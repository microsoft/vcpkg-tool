#include <vcpkg/base/system-headers.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/parallel-algorithms.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <map>
#include <set>

#if defined(__APPLE__)
extern char** environ;
#include <mach-o/dyld.h>
#endif

#if defined(__FreeBSD__) || defined(__OpenBSD__)
extern char** environ;
#include <sys/sysctl.h>
#include <sys/wait.h>
#endif

#if defined(__SVR4) && defined(__sun)
extern char** environ;
#include <stdio.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#include <Psapi.h>
#include <TlHelp32.h>
#include <sddl.h>
#pragma comment(lib, "Advapi32")
#else
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>

#include <sys/wait.h>
#endif

namespace
{
    using namespace vcpkg;

    static std::atomic_int32_t debug_id_counter{1000};
#if defined(_WIN32)
    struct CtrlCStateMachine
    {
        CtrlCStateMachine() : m_number_of_external_processes(0), m_global_job(NULL), m_in_interactive(0) { }

        void transition_to_spawn_process() noexcept
        {
            int cur = 0;
            while (!m_number_of_external_processes.compare_exchange_strong(cur, cur + 1))
            {
                if (cur < 0)
                {
                    // Ctrl-C was hit and is asynchronously executing on another thread.
                    // Some other processes are outstanding.
                    // Sleep forever -- the other process will complete and exit the program
                    while (true)
                    {
                        std::this_thread::sleep_for(std::chrono::seconds(10));
                        msg::println(msgWaitingForChildrenToExit);
                    }
                }
            }
        }
        void transition_from_spawn_process() noexcept
        {
            auto previous = m_number_of_external_processes.fetch_add(-1);
            if (previous == INT_MIN + 1)
            {
                // Ctrl-C was hit while blocked on the child process
                // This is the last external process to complete
                // Therefore, exit
                Checks::log_final_cleanup_and_exit(VCPKG_LINE_INFO, 1);
            }
            else if (previous < 0)
            {
                // Ctrl-C was hit while blocked on the child process
                // Some other processes are outstanding.
                // Sleep forever -- the other process will complete and exit the program
                while (true)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    msg::println(msgWaitingForChildrenToExit);
                }
            }
        }
        void transition_handle_ctrl_c() noexcept
        {
            int old_value = 0;
            while (!m_number_of_external_processes.compare_exchange_strong(old_value, old_value + INT_MIN))
            {
                if (old_value < 0)
                {
                    // Repeat calls to Ctrl-C -- a previous one succeeded.
                    return;
                }
            }

            if (old_value == 0)
            {
                // Not currently blocked on a child process
                Checks::log_final_cleanup_and_exit(VCPKG_LINE_INFO, 1);
            }
            else
            {
                // We are currently blocked on a child process.
                // If none of the child processes are interactive, use the Job Object to terminate the tree.
                if (m_in_interactive.load() == 0)
                {
                    auto job = m_global_job.exchange(NULL);
                    if (job != NULL)
                    {
                        ::CloseHandle(job);
                    }
                }
            }
        }

        void initialize_job()
        {
            m_global_job = CreateJobObjectW(NULL, NULL);
            if (m_global_job != NULL)
            {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
                info.BasicLimitInformation.LimitFlags =
                    JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                ::SetInformationJobObject(m_global_job, JobObjectExtendedLimitInformation, &info, sizeof(info));
                ::AssignProcessToJobObject(m_global_job, ::GetCurrentProcess());
            }
        }

        void enter_interactive() { ++m_in_interactive; }
        void exit_interactive() { --m_in_interactive; }

    private:
        std::atomic<int> m_number_of_external_processes;
        std::atomic<HANDLE> m_global_job;
        std::atomic<int> m_in_interactive;
    };

    static CtrlCStateMachine g_ctrl_c_state;

    struct SpawnProcessGuard
    {
        SpawnProcessGuard() { g_ctrl_c_state.transition_to_spawn_process(); }
        SpawnProcessGuard(const SpawnProcessGuard&) = delete;
        SpawnProcessGuard& operator=(const SpawnProcessGuard&) = delete;
        ~SpawnProcessGuard() { g_ctrl_c_state.transition_from_spawn_process(); }
    };
#endif // ^^^ _WIN32
} // unnamed namespace

namespace vcpkg
{
    void append_shell_escaped(std::string& target, StringView content)
    {
        if (Strings::find_first_of(content, " \t\n\r\"\\`$,;&^|'()") != content.end())
        {
            // TODO: improve this to properly handle all escaping
#if _WIN32
            // On Windows, `\`s before a double-quote must be doubled. Inner double-quotes must be escaped.
            target.push_back('"');
            size_t n_slashes = 0;
            for (auto ch : content)
            {
                if (ch == '\\')
                {
                    ++n_slashes;
                }
                else if (ch == '"')
                {
                    target.append(n_slashes + 1, '\\');
                    n_slashes = 0;
                }
                else
                {
                    n_slashes = 0;
                }
                target.push_back(ch);
            }
            target.append(n_slashes, '\\');
            target.push_back('"');
#else
            // On non-Windows, `\` is the escape character and always requires doubling. Inner double-quotes must be
            // escaped. Additionally, '`' and '$' must be escaped or they will retain their special meaning in the
            // shell.
            target.push_back('"');
            for (auto ch : content)
            {
                if (ch == '\\' || ch == '"' || ch == '`' || ch == '$') target.push_back('\\');
                target.push_back(ch);
            }
            target.push_back('"');
#endif
        }
        else
        {
            target.append(content.data(), content.size());
        }
    }

    static std::atomic<uint64_t> g_subprocess_stats(0);

#if defined(_WIN32)
    void initialize_global_job_object() { g_ctrl_c_state.initialize_job(); }
    void enter_interactive_subprocess() { g_ctrl_c_state.enter_interactive(); }
    void exit_interactive_subprocess() { g_ctrl_c_state.exit_interactive(); }
#endif

    Path get_exe_path_of_current_process()
    {
#if defined(_WIN32)
        wchar_t buf[_MAX_PATH];
        const DWORD bytes = GetModuleFileNameW(nullptr, buf, _MAX_PATH);
        if (bytes == 0) std::abort();
        return Strings::to_utf8(buf, bytes);
#elif defined(__APPLE__)
        static constexpr const uint32_t buff_size = 1024 * 32;
        uint32_t size = buff_size;
        char buf[buff_size] = {};
        int result = _NSGetExecutablePath(buf, &size);
        Checks::check_exit(VCPKG_LINE_INFO, result != -1, "Could not determine current executable path.");
        std::unique_ptr<char> canonicalPath(realpath(buf, NULL));
        Checks::check_exit(VCPKG_LINE_INFO, result != -1, "Could not determine current executable path.");
        return canonicalPath.get();
#elif defined(__FreeBSD__)
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
        char exePath[2048];
        size_t len = sizeof(exePath);
        auto rcode = sysctl(mib, 4, exePath, &len, nullptr, 0);
        Checks::check_exit(VCPKG_LINE_INFO, rcode == 0, "Could not determine current executable path.");
        Checks::check_exit(VCPKG_LINE_INFO, len > 0, "Could not determine current executable path.");
        return Path(exePath, len - 1);
#elif defined(__OpenBSD__)
        int mib[4] = {CTL_KERN, KERN_PROC_ARGS, getpid(), KERN_PROC_ARGV};
        size_t argc = 0;
        auto argc_query = sysctl(mib, 4, nullptr, &argc, nullptr, 0);
        Checks::check_exit(VCPKG_LINE_INFO, argc_query == 0, "Could not determine current executable path.");
        Checks::check_exit(VCPKG_LINE_INFO, argc > 0, "Could not determine current executable path.");
        std::vector<char*> argv(argc);
        auto argv_query = sysctl(mib, 4, &argv[0], &argc, nullptr, 0);
        Checks::check_exit(VCPKG_LINE_INFO, argv_query == 0, "Could not determine current executable path.");
        char buf[PATH_MAX];
        char* exePath(realpath(argv[0], buf));
        Checks::check_exit(VCPKG_LINE_INFO, exePath != nullptr, "Could not determine current executable path.");
        return Path(exePath);
#elif defined(__SVR4) && defined(__sun)
        char procpath[PATH_MAX];
        (void)snprintf(procpath, sizeof(procpath), "/proc/%d/path/a.out", getpid());
        char buf[PATH_MAX];
        auto written = readlink(procpath, buf, sizeof(buf));
        Checks::check_exit(VCPKG_LINE_INFO, written != -1, "Could not determine current executable path.");
        return Path(buf, written);
#else /* LINUX */
        char buf[1024 * 4] = {};
        auto written = readlink("/proc/self/exe", buf, sizeof(buf));
        Checks::check_exit(VCPKG_LINE_INFO, written != -1, "Could not determine current executable path.");
        return Path(buf, written);
#endif
    }

    Optional<ProcessStat> try_parse_process_stat_file(const FileContents& contents)
    {
        ParserBase p(contents.content, contents.origin, {1, 1});

        p.match_while(ParserBase::is_ascii_digit); // pid %d (ignored)

        p.skip_whitespace();
        p.require_character('(');
        // From: https://man7.org/linux/man-pages/man5/procfs.5.html
        //
        //  /proc/[pid]/stat
        //
        //  (2) comm  %s
        //  The filename of the executable, in parentheses.
        //  Strings longer than TASK_COMM_LEN (16) characters (including the terminating null byte) are silently
        //  truncated.  This is visible whether or not the executable is swapped out.
        const auto start = p.it().pointer_to_current();
        const auto end = p.it().end();
        size_t len = 0, last_seen = 0;
        for (auto it = p.it(); len < 17 && it != end; ++len, ++it)
        {
            if (*it == ')') last_seen = len;
        }
        for (size_t i = 0; i < last_seen; ++i)
        {
            p.next();
        }
        p.require_character(')');

        p.skip_whitespace();
        p.next(); // state %c (ignored)

        p.skip_whitespace();
        auto ppid_str = p.match_while(ParserBase::is_ascii_digit);
        auto maybe_ppid = Strings::strto<int>(ppid_str);
        if (auto ppid = maybe_ppid.get())
        {
            return ProcessStat{
                *ppid,
                std::string(start, last_seen),
            };
        }
        return nullopt;
    }
} // namespace vcpkg

namespace
{
#if defined(_WIN32)
    struct ToolHelpProcessSnapshot
    {
        ToolHelpProcessSnapshot() noexcept : snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)) { }
        ToolHelpProcessSnapshot(const ToolHelpProcessSnapshot&) = delete;
        ToolHelpProcessSnapshot& operator=(const ToolHelpProcessSnapshot&) = delete;
        ~ToolHelpProcessSnapshot()
        {
            if (snapshot != INVALID_HANDLE_VALUE)
            {
                CloseHandle(snapshot);
            }
        }
        explicit operator bool() const noexcept { return snapshot != INVALID_HANDLE_VALUE; }

        BOOL Process32First(PPROCESSENTRY32W entry) const noexcept { return ::Process32FirstW(snapshot, entry); }
        BOOL Process32Next(PPROCESSENTRY32W entry) const noexcept { return ::Process32NextW(snapshot, entry); }

    private:
        HANDLE snapshot;
    };
#elif defined(__linux__)
    Optional<ProcessStat> try_get_process_stat_by_pid(int pid)
    {
        auto filepath = fmt::format("/proc/{}/stat", pid);
        auto maybe_contents = real_filesystem.try_read_contents(filepath);
        if (auto contents = maybe_contents.get())
        {
            return try_parse_process_stat_file(*contents);
        }

        return nullopt;
    }
#endif // ^^^ __linux__
} // unnamed namespace

namespace vcpkg
{
    void get_parent_process_list(std::vector<std::string>& ret)
    {
        ret.clear();
#if defined(_WIN32)
        // Enumerate all processes in the system snapshot.
        std::map<DWORD, DWORD> pid_ppid_map;
        std::map<DWORD, std::string> pid_exe_path_map;
        std::set<DWORD> seen_pids;

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        {
            ToolHelpProcessSnapshot snapshot;
            if (!snapshot)
            {
                return;
            }

            if (snapshot.Process32First(&entry))
            {
                do
                {
                    pid_ppid_map.emplace(entry.th32ProcessID, entry.th32ParentProcessID);
                    pid_exe_path_map.emplace(entry.th32ProcessID, Strings::to_utf8(entry.szExeFile));
                } while (snapshot.Process32Next(&entry));
            }
        } // destroy snapshot

        // Find hierarchy of current process

        for (DWORD next_parent = GetCurrentProcessId();;)
        {
            if (Util::Sets::contains(seen_pids, next_parent))
            {
                // parent graph loops, for example if a parent terminates and the PID is reused by a child launch
                break;
            }

            seen_pids.insert(next_parent);
            auto it = pid_ppid_map.find(next_parent);
            if (it == pid_ppid_map.end())
            {
                break;
            }

            ret.push_back(pid_exe_path_map[it->first]);
            next_parent = it->second;
        }
#elif defined(__linux__)
        std::set<int> seen_pids;
        auto maybe_vcpkg_stat = try_get_process_stat_by_pid(getpid());
        if (auto vcpkg_stat = maybe_vcpkg_stat.get())
        {
            for (auto next_parent = vcpkg_stat->ppid; next_parent != 0;)
            {
                if (Util::Sets::contains(seen_pids, next_parent))
                {
                    // parent graph loops, for example if a parent terminates and the PID is reused by a child launch
                    break;
                }

                seen_pids.insert(next_parent);
                auto maybe_next_parent_stat = try_get_process_stat_by_pid(next_parent);
                if (auto next_parent_stat = maybe_next_parent_stat.get())
                {
                    ret.push_back(next_parent_stat->executable_name);
                    next_parent = next_parent_stat->ppid;
                }
                else
                {
                    break;
                }
            }
        }
#endif
    }

    CMakeVariable::CMakeVariable(const StringView varname, const char* varvalue)
        : s(format_cmake_variable(varname, varvalue))
    {
    }
    CMakeVariable::CMakeVariable(const StringView varname, const std::string& varvalue)
        : s(format_cmake_variable(varname, varvalue))
    {
    }
    CMakeVariable::CMakeVariable(const StringView varname, StringLiteral varvalue)
        : s(format_cmake_variable(varname, varvalue))
    {
    }
    CMakeVariable::CMakeVariable(const StringView varname, const Path& varvalue)
        : s(format_cmake_variable(varname, varvalue.generic_u8string()))
    {
    }
    CMakeVariable::CMakeVariable(const std::string& var) : s(var) { }

    std::string format_cmake_variable(StringView key, StringView value) { return fmt::format("-D{}={}", key, value); }

    Command make_basic_cmake_cmd(const Path& cmake_tool_path,
                                 const Path& cmake_script,
                                 const std::vector<CMakeVariable>& pass_variables)
    {
        Command cmd{cmake_tool_path};
        for (auto&& var : pass_variables)
        {
            cmd.string_arg(var.s);
        }
        cmd.string_arg("-P").string_arg(cmake_script);
        return cmd;
    }

    Command& Command::string_arg(StringView s) &
    {
        if (!buf.empty()) buf.push_back(' ');
        append_shell_escaped(buf, s);
        return *this;
    }

    Command& Command::raw_arg(StringView s) &
    {
        if (!buf.empty())
        {
            buf.push_back(' ');
        }

        buf.append(s.data(), s.size());
        return *this;
    }

    Command& Command::forwarded_args(View<std::string> args) &
    {
        for (auto&& arg : args)
        {
            string_arg(arg);
        }

        return *this;
    }

    bool Command::try_append(const Command& other)
    {
        if (buf.size() > maximum_allowed)
        {
            return false;
        }

        if (other.buf.empty())
        {
            return true;
        }

        if (buf.empty())
        {
            if (other.buf.size() > maximum_allowed)
            {
                return false;
            }

            buf = other.buf;
            return true;
        }

        size_t leftover = maximum_allowed - buf.size();
        if (leftover == 0)
        {
            return false;
        }

        --leftover; // for the space
        if (other.buf.size() > leftover)
        {
            return false;
        }

        buf.push_back(' ');
        buf.append(other.buf);
        return true;
    }

#if defined(_WIN32)
    Environment get_modified_clean_environment(const std::unordered_map<std::string, std::string>& extra_env,
                                               StringView prepend_to_path)
    {
        const std::string& system_root_env = get_system_root().value_or_exit(VCPKG_LINE_INFO).native();
        const std::string& system32_env = get_system32().value_or_exit(VCPKG_LINE_INFO).native();
        std::string new_path;
        if (!prepend_to_path.empty())
        {
            Strings::append(new_path, prepend_to_path);
            if (prepend_to_path.back() != ';')
            {
                new_path.push_back(';');
            }
        }

        Strings::append(new_path,
                        system32_env,
                        ';',
                        system_root_env,
                        ';',
                        system32_env,
                        "\\Wbem;",
                        system32_env,
                        "\\WindowsPowerShell\\v1.0\\;",
                        system32_env,
                        "\\OpenSSH\\");

        std::vector<std::string> env_strings = {
            "ALLUSERSPROFILE",
            "APPDATA",
            "CommonProgramFiles",
            "CommonProgramFiles(x86)",
            "CommonProgramW6432",
            "COMPUTERNAME",
            "ComSpec",
            "HOMEDRIVE",
            "HOMEPATH",
            "LOCALAPPDATA",
            "LOGONSERVER",
            "NUMBER_OF_PROCESSORS",
            "OS",
            "PATHEXT",
            "PROCESSOR_ARCHITECTURE",
            "PROCESSOR_ARCHITEW6432",
            "PROCESSOR_IDENTIFIER",
            "PROCESSOR_LEVEL",
            "PROCESSOR_REVISION",
            "ProgramData",
            "ProgramFiles",
            "ProgramFiles(x86)",
            "ProgramW6432",
            "PROMPT",
            "PSModulePath",
            "PUBLIC",
            "SystemDrive",
            "SystemRoot",
            "TEMP",
            "TMP",
            "USERDNSDOMAIN",
            "USERDOMAIN",
            "USERDOMAIN_ROAMINGPROFILE",
            "USERNAME",
            "USERPROFILE",
            "windir",
            // Enables proxy information to be passed to Curl, the underlying download library in cmake.exe
            "http_proxy",
            "https_proxy",
            "no_proxy",
            // Environment variables to tell git to use custom SSH executable or command
            "GIT_SSH",
            "GIT_SSH_COMMAND",
            // Points to a credential-manager binary for git authentication
            "GIT_ASKPASS",
            // Environment variables needed for ssh-agent based authentication
            "SSH_AUTH_SOCK",
            "SSH_AGENT_PID",
            // Enables find_package(CUDA) and enable_language(CUDA) in CMake
            "CUDA_TOOLKIT_ROOT_DIR",
            // Environment variable generated automatically by CUDA after installation
            "NVCUDASAMPLES_ROOT",
            "NVTOOLSEXT_PATH",
            // Enables find_package(Vulkan) in CMake. Environment variable generated by Vulkan SDK installer
            "VULKAN_SDK",
            // Enable targeted Android NDK
            "ANDROID_NDK_HOME",
            // Environment variables generated automatically by Intel oneAPI after installation
            "ONEAPI_ROOT",
            "IFORT_COMPILER19",
            "IFORT_COMPILER20",
            "IFORT_COMPILER21",
            // Environment variables used by wrapper scripts to allow us to set environment variables in parent shells
            "Z_VCPKG_POSTSCRIPT",
            "Z_VCPKG_UNDO",
            // Ensures that the escape hatch persists to recursive vcpkg invocations like x-download
            "VCPKG_KEEP_ENV_VARS",
            // Enables Xbox SDKs
            "GameDKLatest",
            "GRDKLatest",
            "GXDKLatest",
        };

        std::vector<std::string> env_prefix_string = {
            // Enables find_package(CUDA) and enable_language(CUDA) in CMake
            "CUDA_PATH",
        };

        const Optional<std::string> keep_vars = get_environment_variable(EnvironmentVariableVcpkgKeepEnvVars);
        const auto k = keep_vars.get();

        if (k && !k->empty())
        {
            auto vars = Strings::split(*k, ';');

            for (auto&& var : vars)
            {
                if (Strings::case_insensitive_ascii_equals(var, EnvironmentVariablePath))
                {
                    new_path.assign(prepend_to_path.data(), prepend_to_path.size());
                    if (!new_path.empty()) new_path.push_back(';');
                    new_path.append(get_environment_variable(EnvironmentVariablePath).value_or(""));
                }
                else
                {
                    env_strings.emplace_back(std::move(var));
                }
            }
        }

        Environment env;
        std::unordered_set<std::string> env_strings_uppercase;
        for (auto&& env_var : env_strings)
        {
            env_strings_uppercase.emplace(Strings::ascii_to_uppercase(env_var));
        }

        for (auto&& env_var : get_environment_variables())
        {
            auto pos = env_var.find('=');
            auto key = env_var.substr(0, pos);
            if (Util::Sets::contains(env_strings_uppercase, Strings::ascii_to_uppercase(key)) ||
                Util::any_of(env_prefix_string,
                             [&](auto&& group) { return Strings::case_insensitive_ascii_starts_with(key, group); }))
            {
                auto value = pos == std::string::npos ? "" : env_var.substr(pos + 1);
                env.add_entry(key, value);
            }
        }

        const auto path_iter = extra_env.find(EnvironmentVariablePath.to_string());
        if (path_iter != extra_env.end())
        {
            new_path.push_back(';');
            new_path += path_iter->second;
        }
        env.add_entry(EnvironmentVariablePath, new_path);
        // NOTE: we support VS's without the english language pack,
        // but we still want to default to english just in case your specific
        // non-standard build system doesn't support non-english
        env.add_entry(EnvironmentVariableVsLang, "1033");
        env.add_entry(EnvironmentVariableVSCmdSkipSendTelemetry, "1");

        for (const auto& item : extra_env)
        {
            if (item.first == EnvironmentVariablePath) continue;
            env.add_entry(item.first, item.second);
        }

        return env;
    }
#else
    Environment get_modified_clean_environment(const std::unordered_map<std::string, std::string>&,
                                               StringView prepend_to_path)
    {
        Environment env;
        if (!prepend_to_path.empty())
        {
            env.add_entry(
                EnvironmentVariablePath,
                Strings::concat(prepend_to_path,
                                ':',
                                get_environment_variable(EnvironmentVariablePath).value_or_exit(VCPKG_LINE_INFO)));
        }

        return env;
    }
#endif

    void Environment::add_entry(StringView key, StringView value)
    {
#if defined(_WIN32)
        m_env_data.append(Strings::to_utf16(key));
        m_env_data.push_back(L'=');
        m_env_data.append(Strings::to_utf16(value));
        m_env_data.push_back(L'\0');
#else
        Strings::append(m_env_data, key);
        m_env_data.push_back('=');
        append_shell_escaped(m_env_data, value);
        m_env_data.push_back(' ');
#endif
    }

    const Environment::string_t& Environment::get() const { return m_env_data; }

    const Environment& get_clean_environment()
    {
        static const Environment clean_env = get_modified_clean_environment({});
        return clean_env;
    }

    std::vector<ExpectedL<ExitCodeAndOutput>> cmd_execute_and_capture_output_parallel(View<Command> commands)
    {
        RedirectedProcessLaunchSettings default_redirected_process_launch_settings;
        return cmd_execute_and_capture_output_parallel(commands, default_redirected_process_launch_settings);
    }

    std::vector<ExpectedL<ExitCodeAndOutput>> cmd_execute_and_capture_output_parallel(
        View<Command> commands, const RedirectedProcessLaunchSettings& settings)
    {
        std::vector<ExpectedL<ExitCodeAndOutput>> res(commands.size(), LocalizedString{});

        parallel_transform(
            commands, res.begin(), [&](const Command& cmd) { return cmd_execute_and_capture_output(cmd, settings); });

        return res;
    }
} // namespace vcpkg

namespace
{
#if defined(_WIN32)
    void close_handle_mark_invalid(HANDLE& target) noexcept
    {
        auto to_close = std::exchange(target, INVALID_HANDLE_VALUE);
        if (to_close != INVALID_HANDLE_VALUE && to_close)
        {
            CloseHandle(to_close);
        }
    }

    struct ProcessInfo : PROCESS_INFORMATION
    {
        ProcessInfo() noexcept : PROCESS_INFORMATION{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, 0, 0} { }
        ProcessInfo(const ProcessInfo&) = delete;
        ProcessInfo& operator=(const ProcessInfo&) = delete;
        ~ProcessInfo()
        {
            close_handle_mark_invalid(hThread);
            close_handle_mark_invalid(hProcess);
        }

        unsigned long wait()
        {
            close_handle_mark_invalid(hThread);
            const DWORD result = WaitForSingleObject(hProcess, INFINITE);
            Checks::check_exit(VCPKG_LINE_INFO, result != WAIT_FAILED, "WaitForSingleObject failed");
            DWORD exit_code = 0;
            GetExitCodeProcess(hProcess, &exit_code);
            close_handle_mark_invalid(hProcess);
            return exit_code;
        }
    };

    bool windows_create_process(DiagnosticContext& context,
                                std::int32_t debug_id,
                                ProcessInfo& process_info,
                                StringView command_line,
                                const Optional<Path>& working_directory,
                                const Optional<Environment>& environment,
                                BOOL bInheritHandles,
                                DWORD dwCreationFlags,
                                STARTUPINFOEXW& startup_info) noexcept
    {
        Debug::print(fmt::format("{}: CreateProcessW({})\n", debug_id, command_line));

        // Flush stdout before launching external process
        fflush(nullptr);

        Optional<std::wstring> working_directory_wide = working_directory.map([](const Path& wd) {
            // this only fails if we can't get the current working directory of vcpkg, and we assume that we have that,
            // so it's fine anyways
            return Strings::to_utf16(real_filesystem.absolute(wd, VCPKG_LINE_INFO));
        });

        LPCWSTR working_directory_arg = nullptr;
        if (auto wd = working_directory_wide.get())
        {
            working_directory_arg = wd->c_str();
        }

        std::wstring environment_block;
        LPVOID call_environment = nullptr;
        if (auto env_unpacked = environment.get())
        {
            environment_block = env_unpacked->get();
            environment_block.push_back('\0');
            call_environment = environment_block.data();
        }

        // Leaking process information handle 'process_info.proc_info.hProcess'
        // /analyze can't tell that we transferred ownership here
        VCPKG_MSVC_WARNING(suppress : 6335)
        if (!CreateProcessW(nullptr,
                            Strings::to_utf16(command_line).data(),
                            nullptr,
                            nullptr,
                            bInheritHandles,
                            IDLE_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT |
                                dwCreationFlags,
                            call_environment,
                            working_directory_arg,
                            &startup_info.StartupInfo,
                            &process_info))
        {
            context.report_system_error("CreateProcessW", GetLastError());
            return false;
        }

        return true;
    }

    // Used to, among other things, control which handles are inherited by child processes.
    // from https://devblogs.microsoft.com/oldnewthing/20111216-00/?p=8873
    struct ProcAttributeList
    {
        bool create(DiagnosticContext& context, DWORD dwAttributeCount)
        {
            Checks::check_exit(VCPKG_LINE_INFO, buffer.empty());
            SIZE_T size = 0;
            if (InitializeProcThreadAttributeList(nullptr, dwAttributeCount, 0, &size) ||
                GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            {
                context.report_system_error("InitializeProcThreadAttributeList nullptr", GetLastError());
                return false;
            }
            Checks::check_exit(VCPKG_LINE_INFO, size > 0);
            ASSUME(size > 0);
            buffer.resize(size);
            if (!InitializeProcThreadAttributeList(
                    reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data()), dwAttributeCount, 0, &size))
            {
                context.report_system_error("InitializeProcThreadAttributeList attribute_list", GetLastError());
                return false;
            }

            return true;
        }
        bool update_attribute(DiagnosticContext& context, DWORD_PTR Attribute, PVOID lpValue, SIZE_T cbSize)
        {
            if (!UpdateProcThreadAttribute(get(), 0, Attribute, lpValue, cbSize, nullptr, nullptr))
            {
                context.report_system_error("InitializeProcThreadAttributeList attribute_list", GetLastError());
                return false;
            }

            return true;
        }
        LPPROC_THREAD_ATTRIBUTE_LIST get() noexcept
        {
            return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        }

        ProcAttributeList() = default;
        ProcAttributeList(const ProcAttributeList&) = delete;
        ProcAttributeList& operator=(const ProcAttributeList&) = delete;
        ~ProcAttributeList()
        {
            if (!buffer.empty())
            {
                DeleteProcThreadAttributeList(get());
            }
        }

    private:
        std::vector<unsigned char> buffer;
    };

    struct AnonymousPipe
    {
        HANDLE read_pipe = INVALID_HANDLE_VALUE;
        HANDLE write_pipe = INVALID_HANDLE_VALUE;

        AnonymousPipe() = default;
        AnonymousPipe(const AnonymousPipe&) = delete;
        AnonymousPipe& operator=(const AnonymousPipe&) = delete;
        ~AnonymousPipe()
        {
            close_handle_mark_invalid(read_pipe);
            close_handle_mark_invalid(write_pipe);
        }

        bool create(DiagnosticContext& context)
        {
            Checks::check_exit(VCPKG_LINE_INFO, read_pipe == INVALID_HANDLE_VALUE);
            Checks::check_exit(VCPKG_LINE_INFO, write_pipe == INVALID_HANDLE_VALUE);
            SECURITY_ATTRIBUTES anonymousSa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
            if (!CreatePipe(&read_pipe, &write_pipe, &anonymousSa, 0))
            {
                context.report_system_error("CreatePipe", GetLastError());
                return false;
            }

            return true;
        }
    };

    struct CreatorOnlySecurityDescriptor
    {
        PSECURITY_DESCRIPTOR sd;

        CreatorOnlySecurityDescriptor() : sd{}
        {
            // DACL:
            //  ACE 0: Allow; FILE_READ;;;OWNER_RIGHTS
            Checks::check_exit(
                VCPKG_LINE_INFO,
                ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;FR;;;OW)", SDDL_REVISION_1, &sd, 0));
        }

        ~CreatorOnlySecurityDescriptor() { LocalFree(sd); }

        CreatorOnlySecurityDescriptor(const CreatorOnlySecurityDescriptor&) = delete;
        CreatorOnlySecurityDescriptor& operator=(const CreatorOnlySecurityDescriptor&) = delete;
    };

    // An output pipe to use as stdin for a child process
    struct OverlappedOutputPipe
    {
        HANDLE read_pipe = INVALID_HANDLE_VALUE;
        HANDLE write_pipe = INVALID_HANDLE_VALUE;

        OverlappedOutputPipe() = default;
        OverlappedOutputPipe(const OverlappedOutputPipe&) = delete;
        OverlappedOutputPipe& operator=(const OverlappedOutputPipe&) = delete;
        ~OverlappedOutputPipe()
        {
            close_handle_mark_invalid(read_pipe);
            close_handle_mark_invalid(write_pipe);
        }

        bool create(DiagnosticContext& context, std::int32_t debug_id)
        {
            Checks::check_exit(VCPKG_LINE_INFO, read_pipe == INVALID_HANDLE_VALUE);
            Checks::check_exit(VCPKG_LINE_INFO, write_pipe == INVALID_HANDLE_VALUE);

            static CreatorOnlySecurityDescriptor creator_owner_sd;
            SECURITY_ATTRIBUTES namedPipeSa{sizeof(SECURITY_ATTRIBUTES), creator_owner_sd.sd, FALSE};
            std::wstring pipe_name{Strings::to_utf16(
                fmt::format(R"(\\.\pipe\local\vcpkg-to-stdin-A8B4F218-4DB1-4A3E-8E5B-C41F1633F627-{}-{})",
                            GetCurrentProcessId(),
                            debug_id))};
            write_pipe = CreateNamedPipeW(pipe_name.c_str(),
                                          PIPE_ACCESS_OUTBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
                                          PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
                                          1,     // nMaxInstances
                                          65535, // nOutBufferSize
                                          0,     // nInBufferSize (unused / PIPE_ACCESS_OUTBOUND)
                                          0,     // nDefaultTimeout (only for WaitPipe; unused)
                                          &namedPipeSa);
            if (write_pipe == INVALID_HANDLE_VALUE)
            {
                context.report_system_error("CreateNamedPipeW stdin", GetLastError());
                return false;
            }

            SECURITY_ATTRIBUTES openSa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
            read_pipe = CreateFileW(pipe_name.c_str(), FILE_GENERIC_READ, 0, &openSa, OPEN_EXISTING, 0, 0);
            if (read_pipe == INVALID_HANDLE_VALUE)
            {
                context.report_system_error("CreateFileW stdin", GetLastError());
                return false;
            }

            return true;
        }
    };

    // Ensure that all asynchronous procedure calls pending for this thread are called
    void drain_apcs()
    {
        switch (SleepEx(0, TRUE))
        {
            case 0:
                // timeout expired, OK
                break;
            case WAIT_IO_COMPLETION:
                // completion queue drained completed, OK
                break;
            default: vcpkg::Checks::unreachable(VCPKG_LINE_INFO); break;
        }
    }

    struct OverlappedStatus : OVERLAPPED
    {
        DWORD expected_write;
        HANDLE* target;
        int32_t debug_id;
    };

    struct RedirectedProcessInfo
    {
        AnonymousPipe stdout_pipe;
        OverlappedOutputPipe stdin_pipe;
        ProcessInfo proc_info;

        RedirectedProcessInfo() = default;
        RedirectedProcessInfo(const RedirectedProcessInfo&) = delete;
        RedirectedProcessInfo& operator=(const RedirectedProcessInfo&) = delete;
        ~RedirectedProcessInfo() = default;

        VCPKG_MSVC_WARNING(suppress : 6262) // function uses 32k of stack
        unsigned long wait_and_stream_output(int32_t debug_id,
                                             const char* input,
                                             DWORD input_size,
                                             const std::function<void(char*, size_t)>& raw_cb)
        {
            static const auto stdin_completion_routine =
                [](DWORD dwErrorCode, DWORD dwNumberOfBytesTransferred, LPOVERLAPPED pOverlapped) {
                    const auto status = static_cast<OverlappedStatus*>(pOverlapped);
                    switch (dwErrorCode)
                    {
                        case 0:
                            // OK, done
                            Checks::check_exit(VCPKG_LINE_INFO, dwNumberOfBytesTransferred == status->expected_write);
                            break;
                        case ERROR_BROKEN_PIPE:
                        case ERROR_OPERATION_ABORTED:
                            // OK, child didn't want all the data
                            break;
                        default:
                            Debug::print(fmt::format("{}: Unexpected error writing to stdin of a child process: {:X}\n",
                                                     status->debug_id,
                                                     dwErrorCode));
                            break;
                    }

                    close_handle_mark_invalid(*status->target);
                };

            OverlappedStatus stdin_write{};
            stdin_write.expected_write = input_size;
            stdin_write.target = &stdin_pipe.write_pipe;
            stdin_write.debug_id = debug_id;
            if (input_size == 0)
            {
                close_handle_mark_invalid(stdin_pipe.write_pipe);
            }
            else
            {
                stdin_write.expected_write = input_size;
                if (WriteFileEx(stdin_pipe.write_pipe, input, input_size, &stdin_write, stdin_completion_routine))
                {
                    DWORD last_error = GetLastError();
                    if (last_error)
                    {
                        Debug::print(
                            fmt::format("{}: Unexpected WriteFileEx partial success: {:X}\n", debug_id, last_error));
                    }
                }
                else
                {
                    Debug::print(fmt::format("{}: stdin WriteFileEx failure: {:x}\n", debug_id, GetLastError()));
                    close_handle_mark_invalid(stdin_pipe.write_pipe);
                }
            }

            DWORD bytes_read = 0;
            static constexpr DWORD buffer_size = 1024 * 32;
            char buf[buffer_size];
            while (stdout_pipe.read_pipe != INVALID_HANDLE_VALUE)
            {
                switch (WaitForSingleObjectEx(stdout_pipe.read_pipe, INFINITE, TRUE))
                {
                    case WAIT_OBJECT_0:
                        if (ReadFile(stdout_pipe.read_pipe, static_cast<void*>(buf), buffer_size, &bytes_read, nullptr))
                        {
                            raw_cb(buf, bytes_read);
                        }
                        else
                        {
                            DWORD last_error = GetLastError();
                            if (last_error != ERROR_BROKEN_PIPE)
                            {
                                Debug::print(fmt::format("{}: Writing to stdout failed: {:x}\n", debug_id, last_error));
                            }

                            close_handle_mark_invalid(stdout_pipe.read_pipe);
                        }

                        break;
                    case WAIT_IO_COMPLETION:
                        // stdin might have completed, that's OK
                        break;
                    case WAIT_FAILED:
                        vcpkg::Checks::unreachable(
                            VCPKG_LINE_INFO,
                            fmt::format("{}: Waiting for stdout failed: {:x}", debug_id, GetLastError()));
                        break;
                    default: vcpkg::Checks::unreachable(VCPKG_LINE_INFO); break;
                }
            }

            auto child_exit_code = proc_info.wait();
            drain_apcs();
            if (stdin_pipe.write_pipe != INVALID_HANDLE_VALUE)
            {
                // this block probably never runs
                Debug::print(fmt::format("{}: stdin write outlived the child process?\n", debug_id));
                if (CancelIo(stdin_pipe.write_pipe))
                {
                    drain_apcs();
                }
                else
                {
                    Debug::print(fmt::format("{}: Cancelling stdin write failed: {:x}\n", debug_id, GetLastError()));
                }
            }

            return child_exit_code;
        }
    };
#else // ^^^ _WIN32 // !_WIN32 vvv
    struct AnonymousPipe
    {
        // pipefd[0] is the read end of the pipe, pipefd[1] is the write end
        int pipefd[2];

        AnonymousPipe() : pipefd{-1, -1} { }
        AnonymousPipe(const AnonymousPipe&) = delete;
        AnonymousPipe& operator=(const AnonymousPipe&) = delete;
        ~AnonymousPipe()
        {
            for (size_t idx = 0; idx < 2; ++idx)
            {
                close_mark_invalid(pipefd[idx]);
            }
        }

        bool create(DiagnosticContext& context)
        {
#if defined(__APPLE__)
            static std::mutex pipe_creation_lock;
            std::lock_guard<std::mutex> lck{pipe_creation_lock};
            if (pipe(pipefd))
            {
                context.report_system_error("pipe", errno);
                return false;
            }

            for (size_t idx = 0; idx < 2; ++idx)
            {
                if (fcntl(pipefd[idx], F_SETFD, FD_CLOEXEC))
                {
                    context.report_system_error("fcntl", errno);
                    return false;
                }
            }
#else  // ^^^ Apple // !Apple vvv
            if (pipe2(pipefd, O_CLOEXEC))
            {
                context.report_system_error("pipe2", errno);
                return false;
            }
#endif // ^^^ !Apple

            return true;
        }
    };

    struct PosixSpawnFileActions
    {
        posix_spawn_file_actions_t actions;

        PosixSpawnFileActions() { Checks::check_exit(VCPKG_LINE_INFO, posix_spawn_file_actions_init(&actions) == 0); }

        ~PosixSpawnFileActions()
        {
            Checks::check_exit(VCPKG_LINE_INFO, posix_spawn_file_actions_destroy(&actions) == 0);
        }

        PosixSpawnFileActions(const PosixSpawnFileActions&) = delete;
        PosixSpawnFileActions& operator=(const PosixSpawnFileActions&) = delete;

        bool adddup2(DiagnosticContext& context, int fd, int newfd)
        {
            const int error = posix_spawn_file_actions_adddup2(&actions, fd, newfd);
            if (error)
            {
                context.report_system_error("posix_spawn_file_actions_adddup2", error);
                return false;
            }

            return true;
        }
    };

    struct PosixPid
    {
        pid_t pid;

        PosixPid() : pid{-1} { }

        Optional<int> wait_for_termination(DiagnosticContext& context)
        {
            int exit_code = -1;
            if (pid != -1)
            {
                int status;
                pid_t child;
                do
                {
                    child = waitpid(pid, &status, 0);
                } while (child == -1 && errno == EINTR);
                if (child != pid)
                {
                    context.report_system_error("waitpid", errno);
                    return nullopt;
                }

                if (WIFEXITED(status))
                {
                    exit_code = WEXITSTATUS(status);
                }
                else if (WIFSIGNALED(status))
                {
                    exit_code = WTERMSIG(status);
                }
                else if (WIFSTOPPED(status))
                {
                    exit_code = WSTOPSIG(status);
                }

                pid = -1;
            }

            return exit_code;
        }

        PosixPid(const PosixPid&) = delete;
        PosixPid& operator=(const PosixPid&) = delete;
    };
#endif // ^^^ !_WIN32
} // unnamed namespace

namespace vcpkg
{
#if defined(_WIN32)
    Environment cmd_execute_and_capture_environment(const Command& cmd, const Environment& env)
    {
        static StringLiteral magic_string = "cdARN4xjKueKScMy9C6H";

        Command actual_cmd = cmd;
        actual_cmd.raw_arg(Strings::concat(" & echo ", magic_string, " & set"));

        Debug::print("command line: ", actual_cmd.command_line(), "\n");

        RedirectedProcessLaunchSettings settings;
        settings.environment = env;
        settings.create_new_console = CreateNewConsole::Yes;
        auto maybe_rc_output = cmd_execute_and_capture_output(actual_cmd, settings);
        if (!maybe_rc_output)
        {
            Checks::msg_exit_with_error(
                VCPKG_LINE_INFO, msg::format(msgVcvarsRunFailed).append_raw("\n").append(maybe_rc_output.error()));
        }

        auto& rc_output = maybe_rc_output.value_or_exit(VCPKG_LINE_INFO);
        Debug::print(rc_output.output, "\n");
        if (rc_output.exit_code != 0)
        {
            Checks::msg_exit_with_error(
                VCPKG_LINE_INFO, msgVcvarsRunFailedExitCode, msg::exit_code = rc_output.exit_code);
        }

        auto it = Strings::search(rc_output.output, magic_string);
        const char* const last = rc_output.output.data() + rc_output.output.size();

        Checks::check_exit(VCPKG_LINE_INFO, it != last);
        // find the first non-whitespace character after the magic string
        it = std::find_if_not(it + magic_string.size(), last, ::isspace);
        Checks::check_exit(VCPKG_LINE_INFO, it != last);

        Environment new_env;

        for (;;)
        {
            auto equal_it = std::find(it, last, '=');
            if (equal_it == last) break;
            StringView variable_name(it, equal_it);
            auto newline_it = std::find(equal_it + 1, last, '\r');
            if (newline_it == last) break;
            StringView value(equal_it + 1, newline_it);

            new_env.add_entry(variable_name, value);

            it = newline_it + 1;
            if (it != last && *it == '\n') ++it;
        }

        return new_env;
    }
#endif

    void cmd_execute_background(const Command& cmd_line)
    {
        const auto debug_id = debug_id_counter.fetch_add(1, std::memory_order_relaxed);
        Debug::print(fmt::format("{}: cmd_execute_background: {}\n", debug_id, cmd_line.command_line()));
#if defined(_WIN32)
        ProcessInfo process_info;
        STARTUPINFOEXW startup_info_ex;
        memset(&startup_info_ex, 0, sizeof(STARTUPINFOEXW));
        startup_info_ex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        startup_info_ex.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startup_info_ex.StartupInfo.wShowWindow = SW_HIDE;
        (void)windows_create_process(Debug::g_debugging ? console_diagnostic_context : null_diagnostic_context,
                                     debug_id,
                                     process_info,
                                     cmd_line.command_line(),
                                     nullopt,
                                     nullopt,
                                     FALSE,
                                     CREATE_NEW_CONSOLE | CREATE_BREAKAWAY_FROM_JOB,
                                     startup_info_ex);
#else  // ^^^ _WIN32 // !_WIN32
        pid_t pid;

        std::vector<std::string> argv_builder; // as if by system()
        argv_builder.reserve(3);
        argv_builder.emplace_back("sh");
        argv_builder.emplace_back("-c");
        StringView command_line = cmd_line.command_line();
        argv_builder.emplace_back(command_line.data(), command_line.size());

        std::vector<char*> argv;
        argv.reserve(argv_builder.size() + 1);
        for (std::string& arg : argv_builder)
        {
            argv.emplace_back(arg.data());
        }

        argv.emplace_back(nullptr);

        int error = posix_spawn(&pid, "/bin/sh", nullptr /*file_actions*/, nullptr /*attrp*/, argv.data(), environ);
        if (error && Debug::g_debugging)
        {
            console_diagnostic_context.report_system_error("posix_spawn", errno);
        }
#endif // ^^^ !_WIN32
    }

    static Optional<ExitCodeIntegral> cmd_execute_impl(DiagnosticContext& context,
                                                       const Command& cmd,
                                                       const ProcessLaunchSettings& settings,
                                                       const int32_t debug_id)
    {
#if defined(_WIN32)
        STARTUPINFOEXW startup_info_ex;
        memset(&startup_info_ex, 0, sizeof(STARTUPINFOEXW));
        startup_info_ex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        startup_info_ex.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startup_info_ex.StartupInfo.wShowWindow = SW_HIDE;

        ProcAttributeList proc_attribute_list;
        auto proc_attribute_list_create = proc_attribute_list.create(context, 1);
        if (!proc_attribute_list_create)
        {
            return nullopt;
        }

        constexpr size_t number_of_candidate_handles = 3;
        HANDLE handles_to_inherit[number_of_candidate_handles] = {
            GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE)};
        Util::sort(handles_to_inherit);
        size_t number_of_handles =
            std::unique(handles_to_inherit, handles_to_inherit + number_of_candidate_handles) - handles_to_inherit;

        if (!proc_attribute_list.update_attribute(
                context, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles_to_inherit, number_of_handles * sizeof(HANDLE)))
        {
            return nullopt;
        }

        startup_info_ex.lpAttributeList = proc_attribute_list.get();

        SpawnProcessGuard spawn_process_guard;
        ProcessInfo process_info;
        auto process_create = windows_create_process(context,
                                                     debug_id,
                                                     process_info,
                                                     cmd.command_line(),
                                                     settings.working_directory,
                                                     settings.environment,
                                                     TRUE,
                                                     0,
                                                     startup_info_ex);
        if (!process_create)
        {
            return nullopt;
        }

        return process_info.wait();
#else
        (void)context;
        Command real_command_line_builder;
        if (const auto wd = settings.working_directory.get())
        {
            real_command_line_builder.string_arg("cd");
            real_command_line_builder.string_arg(*wd);
            real_command_line_builder.raw_arg("&&");
        }

        if (const auto env = settings.environment.get())
        {
            real_command_line_builder.raw_arg(env->get());
        }

        real_command_line_builder.raw_arg(cmd.command_line());

        std::string real_command_line = std::move(real_command_line_builder).extract();
        Debug::print(fmt::format("{}: system({})\n", debug_id, real_command_line));
        fflush(nullptr);

        // CodeQL [cpp/uncontrolled-process-operation]: This is intended to run whatever process the user supplies.
        return system(real_command_line.c_str());
#endif
    }

    Optional<ExitCodeIntegral> cmd_execute(DiagnosticContext& context, const Command& cmd)
    {
        ProcessLaunchSettings default_process_launch_settings;
        return cmd_execute(context, cmd, default_process_launch_settings);
    }

    Optional<ExitCodeIntegral> cmd_execute(DiagnosticContext& context,
                                           const Command& cmd,
                                           const ProcessLaunchSettings& settings)
    {
        const ElapsedTimer timer;
        const auto debug_id = debug_id_counter.fetch_add(1, std::memory_order_relaxed);
        auto maybe_exit_code = cmd_execute_impl(context, cmd, settings, debug_id);
        const auto elapsed = timer.us_64();
        g_subprocess_stats += elapsed;
        if (auto exit_code = maybe_exit_code.get())
        {
            Debug::print(fmt::format("{}: child process returned {} after {} us\n", debug_id, *exit_code, elapsed));
        }
        else
        {
            Debug::print(
                fmt::format("{}: cmd_execute() failed to launch child process after {} us\n", debug_id, elapsed));
        }

        return maybe_exit_code;
    }

    Optional<ExitCodeIntegral> cmd_execute_and_stream_lines(DiagnosticContext& context,
                                                            const Command& cmd,
                                                            const std::function<void(StringView)>& per_line_cb)
    {
        RedirectedProcessLaunchSettings default_redirected_process_launch_settings;
        return cmd_execute_and_stream_lines(context, cmd, default_redirected_process_launch_settings, per_line_cb);
    }

    Optional<ExitCodeIntegral> cmd_execute_and_stream_lines(DiagnosticContext& context,
                                                            const Command& cmd,
                                                            const RedirectedProcessLaunchSettings& settings,
                                                            const std::function<void(StringView)>& per_line_cb)
    {
        Strings::LinesStream lines;
        auto rc = cmd_execute_and_stream_data(
            context, cmd, settings, [&](const StringView sv) { lines.on_data(sv, per_line_cb); });
        lines.on_end(per_line_cb);
        return rc;
    }

} // namespace vcpkg

namespace
{
#if !defined(_WIN32)
    struct ChildStdinTracker
    {
        StringView input;
        std::size_t offset;

        // Write a hunk of data to `target`. If there is no more input to write, returns `true`.
        Optional<bool> do_write(DiagnosticContext& context, int target)
        {
            const auto this_write = input.size() - offset;
            // Big enough to be big, small enough to avoid implementation limits
            static constexpr std::size_t max_write = 1 << 28;
            if (this_write != 0)
            {
                const auto this_write_clamped = this_write > max_write ? max_write : this_write;
                const auto actually_written =
                    write(target, static_cast<const void*>(input.data() + offset), this_write_clamped);
                if (actually_written < 0)
                {
                    context.report_system_error("write", errno);
                    return nullopt;
                }

                offset += actually_written;
            }

            return offset == input.size();
        }
    };
#endif // ^^^ !_WIN32

    Optional<ExitCodeIntegral> cmd_execute_and_stream_data_impl(DiagnosticContext& context,
                                                                const Command& cmd,
                                                                const RedirectedProcessLaunchSettings& settings,
                                                                const std::function<void(StringView)>& data_cb,
                                                                uint32_t debug_id)
    {
#if defined(_WIN32)
        std::wstring as_utf16;
        StringView stdin_content = settings.stdin_content;
        if (!stdin_content.empty() && settings.encoding == Encoding::Utf16)
        {
            as_utf16 = Strings::to_utf16(stdin_content);
            stdin_content =
                StringView{reinterpret_cast<const char*>(as_utf16.data()), as_utf16.size() * sizeof(wchar_t)};
        }

        auto stdin_content_size_raw = stdin_content.size();
        if (stdin_content_size_raw > MAXDWORD)
        {
            context.report_system_error("WriteFileEx", ERROR_INSUFFICIENT_BUFFER);
            return nullopt;
        }

        auto stdin_content_size = static_cast<DWORD>(stdin_content_size_raw);

        SpawnProcessGuard spawn_process_guard;
        RedirectedProcessInfo process_info;
        DWORD dwCreationFlags = 0;
        STARTUPINFOEXW startup_info_ex{};
        startup_info_ex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        startup_info_ex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        switch (settings.create_new_console)
        {
            case CreateNewConsole::No: break;
            case CreateNewConsole::Yes:
                dwCreationFlags |= CREATE_NEW_CONSOLE;
                startup_info_ex.StartupInfo.dwFlags |= STARTF_USESHOWWINDOW;
                startup_info_ex.StartupInfo.wShowWindow = SW_HIDE;
                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        // Create a pipe for the child process's STDIN.
        if (!process_info.stdin_pipe.create(context, debug_id))
        {
            return nullopt;
        }

        startup_info_ex.StartupInfo.hStdInput = process_info.stdin_pipe.read_pipe;

        // Create a pipe for the child process's STDOUT/STDERR.
        if (!process_info.stdout_pipe.create(context))
        {
            return nullopt;
        }

        startup_info_ex.StartupInfo.hStdOutput = process_info.stdout_pipe.write_pipe;
        startup_info_ex.StartupInfo.hStdError = process_info.stdout_pipe.write_pipe;

        ProcAttributeList proc_attribute_list;
        if (!proc_attribute_list.create(context, 1))
        {
            return nullopt;
        }

        HANDLE handles_to_inherit[2] = {startup_info_ex.StartupInfo.hStdOutput, startup_info_ex.StartupInfo.hStdInput};
        if (!proc_attribute_list.update_attribute(
                context, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles_to_inherit, 2 * sizeof(HANDLE)))
        {
            return nullopt;
        }

        startup_info_ex.lpAttributeList = proc_attribute_list.get();

        if (!windows_create_process(context,
                                    debug_id,
                                    process_info.proc_info,
                                    cmd.command_line(),
                                    settings.working_directory,
                                    settings.environment,
                                    TRUE,
                                    dwCreationFlags,
                                    startup_info_ex))
        {
            return nullopt;
        }

        close_handle_mark_invalid(process_info.stdin_pipe.read_pipe);
        close_handle_mark_invalid(process_info.stdout_pipe.write_pipe);

        std::function<void(char*, size_t)> raw_cb;
        switch (settings.encoding)
        {
            case Encoding::Utf8:
                raw_cb = [&](char* buf, size_t bytes_read) {
                    std::replace(buf, buf + bytes_read, '\0', '?');
                    if (settings.echo_in_debug == EchoInDebug::Show && Debug::g_debugging)
                    {
                        msg::write_unlocalized_text_to_stdout(Color::none, StringView{buf, bytes_read});
                    }

                    data_cb(StringView{buf, bytes_read});
                };
                break;
            case Encoding::Utf16:
                raw_cb = [&](char* buf, size_t bytes_read) {
                    // Note: This doesn't handle unpaired surrogates or partial encoding units correctly in
                    // order to be able to reuse Strings::to_utf8 which we believe will be fine 99% of the time.
                    std::string encoded;
                    Strings::to_utf8(encoded, reinterpret_cast<const wchar_t*>(buf), bytes_read / 2);
                    std::replace(encoded.begin(), encoded.end(), '\0', '?');
                    if (settings.echo_in_debug == EchoInDebug::Show && Debug::g_debugging)
                    {
                        msg::write_unlocalized_text_to_stdout(Color::none, StringView{encoded});
                    }

                    data_cb(StringView{encoded});
                };
                break;
            case Encoding::Utf8WithNulls:
                raw_cb = [&](char* buf, size_t bytes_read) {
                    if (settings.echo_in_debug == EchoInDebug::Show && Debug::g_debugging)
                    {
                        msg::write_unlocalized_text_to_stdout(Color::none,
                                                              Strings::replace_all(StringView{buf, bytes_read},
                                                                                   StringLiteral{"\0"},
                                                                                   StringLiteral{"\\0"}));
                    }

                    data_cb(StringView{buf, bytes_read});
                };
                break;
            default: vcpkg::Checks::unreachable(VCPKG_LINE_INFO);
        }

        return process_info.wait_and_stream_output(debug_id, stdin_content.data(), stdin_content_size, raw_cb);
#else  // ^^^ _WIN32 // !_WIN32 vvv

        std::string actual_cmd_line;
        if (auto wd = settings.working_directory.get())
        {
            actual_cmd_line.append("cd ");
            append_shell_escaped(actual_cmd_line, *wd);
            actual_cmd_line.append(" && ");
        }

        if (auto env_unpacked = settings.environment.get())
        {
            actual_cmd_line.append(env_unpacked->get());
            actual_cmd_line.push_back(' ');
        }

        const auto unwrapped_to_execute = cmd.command_line();
        actual_cmd_line.append(unwrapped_to_execute.data(), unwrapped_to_execute.size());

        Debug::print(fmt::format("{}: execute_process({})\n", debug_id, actual_cmd_line));
        // Flush stdout before launching external process
        fflush(stdout);

        AnonymousPipe child_input;
        if (!child_input.create(context))
        {
            return nullopt;
        }

        AnonymousPipe child_output;
        if (!child_output.create(context))
        {
            return nullopt;
        }

        PosixSpawnFileActions actions;
        if (!actions.adddup2(context, child_input.pipefd[0], 0))
        {
            return nullopt;
        }

        if (!actions.adddup2(context, child_output.pipefd[1], 1))
        {
            return nullopt;
        }

        if (!actions.adddup2(context, child_output.pipefd[1], 2))
        {
            return nullopt;
        }

        std::vector<std::string> argv_builder;
        argv_builder.reserve(3);
        argv_builder.emplace_back("sh"); // as if by system()
        argv_builder.emplace_back("-c");
        argv_builder.emplace_back(actual_cmd_line.data(), actual_cmd_line.size());

        std::vector<char*> argv;
        argv.reserve(argv_builder.size() + 1);
        for (std::string& arg : argv_builder)
        {
            argv.emplace_back(arg.data());
        }

        argv.emplace_back(nullptr);

        PosixPid pid;
        int error = posix_spawn(&pid.pid, "/bin/sh", &actions.actions, nullptr, argv.data(), environ);
        if (error)
        {
            context.report_system_error("posix_spawn", error);
            return nullopt;
        }

        close_mark_invalid(child_input.pipefd[0]);
        close_mark_invalid(child_output.pipefd[1]);

        char buf[1024];
        ChildStdinTracker stdin_tracker{settings.stdin_content, 0};
        if (settings.stdin_content.empty())
        {
            close_mark_invalid(child_input.pipefd[1]);
        }
        else
        {
            if (fcntl(child_input.pipefd[1], F_SETFL, O_NONBLOCK))
            {
                context.report_system_error("fcntl", errno);
                return nullopt;
            }

            auto maybe_done = stdin_tracker.do_write(context, child_input.pipefd[1]);
            bool done = false;
            if (const auto done_first = maybe_done.get())
            {
                if (*done_first)
                {
                    close_mark_invalid(child_input.pipefd[1]);
                    done = true;
                }
            }
            else
            {
                return nullopt;
            }

            if (!done)
            {
                for (;;)
                {
                    pollfd polls[2]{};
                    polls[0].fd = child_input.pipefd[1];
                    polls[0].events = POLLOUT;
                    polls[1].fd = child_output.pipefd[0];
                    polls[1].events = POLLIN;
                    if (poll(polls, 2, -1) < 0)
                    {
                        context.report_system_error("poll", errno);
                        return nullopt;
                    }

                    if (polls[0].revents & POLLERR)
                    {
                        close_mark_invalid(child_input.pipefd[1]);
                        break;
                    }
                    else if (polls[0].revents & POLLOUT)
                    {
                        auto maybe_next_done = stdin_tracker.do_write(context, child_input.pipefd[1]);
                        if (const auto next_done = maybe_next_done.get())
                        {
                            if (*next_done)
                            {
                                close_mark_invalid(child_input.pipefd[1]);
                                break;
                            }
                        }
                        else
                        {
                            return nullopt;
                        }
                    }

                    if (polls[1].revents & POLLIN)
                    {
                        auto read_amount = read(child_output.pipefd[0], buf, sizeof(buf));
                        if (read_amount < 0)
                        {
                            context.report_system_error("read", errno);
                            return nullopt;
                        }

                        // can't be 0 because poll told us otherwise
                        if (read_amount == 0)
                        {
                            Checks::unreachable(VCPKG_LINE_INFO);
                        }

                        StringView this_read_data{buf, static_cast<size_t>(read_amount)};
                        data_cb(this_read_data);
                        if (settings.echo_in_debug == EchoInDebug::Show && Debug::g_debugging)
                        {
                            msg::write_unlocalized_text(Color::none, this_read_data);
                        }
                    }
                }
            }
        }

        for (;;)
        {
            auto read_amount = read(child_output.pipefd[0], buf, sizeof(buf));
            if (read_amount < 0)
            {
                auto error = errno;
                if (error == EPIPE)
                {
                    close_mark_invalid(child_output.pipefd[0]);
                    break;
                }

                context.report_system_error("read", errno);
                return nullopt;
            }

            if (read_amount == 0)
            {
                close_mark_invalid(child_output.pipefd[0]);
                break;
            }

            StringView this_read_data{buf, static_cast<size_t>(read_amount)};
            switch (settings.encoding)
            {
                case Encoding::Utf8:
                    std::replace(buf, buf + read_amount, '\0', '?');
                    if (settings.echo_in_debug == EchoInDebug::Show && Debug::g_debugging)
                    {
                        msg::write_unlocalized_text(Color::none, this_read_data);
                    }
                    break;
                case Encoding::Utf8WithNulls:
                    if (settings.echo_in_debug == EchoInDebug::Show && Debug::g_debugging)
                    {
                        msg::write_unlocalized_text_to_stdout(
                            Color::none,
                            Strings::replace_all(this_read_data, StringLiteral{"\0"}, StringLiteral{"\\0"}));
                    }

                    break;
                default: Checks::unreachable(VCPKG_LINE_INFO); break;
            }

            data_cb(this_read_data);
        }

        return pid.wait_for_termination(context);
#endif /// ^^^ !_WIN32
    }
} // unnamed namespace

namespace vcpkg
{
    Optional<ExitCodeIntegral> cmd_execute_and_stream_data(DiagnosticContext& context,
                                                           const Command& cmd,
                                                           const std::function<void(StringView)>& data_cb)
    {
        RedirectedProcessLaunchSettings default_redirected_process_launch_settings;
        return cmd_execute_and_stream_data(context, cmd, default_redirected_process_launch_settings, data_cb);
    }

    Optional<ExitCodeIntegral> cmd_execute_and_stream_data(DiagnosticContext& context,
                                                           const Command& cmd,
                                                           const RedirectedProcessLaunchSettings& settings,
                                                           const std::function<void(StringView)>& data_cb)
    {
        const ElapsedTimer timer;
        const auto debug_id = debug_id_counter.fetch_add(1, std::memory_order_relaxed);
        auto maybe_exit_code = cmd_execute_and_stream_data_impl(context, cmd, settings, data_cb, debug_id);
        const auto elapsed = timer.us_64();
        g_subprocess_stats += elapsed;
        if (const auto exit_code = maybe_exit_code.get())
        {
            Debug::print(fmt::format("{}: cmd_execute_and_stream_data() returned {} after {:8} us\n",
                                     debug_id,
                                     *exit_code,
                                     static_cast<unsigned long long>(elapsed)));
        }

        return maybe_exit_code;
    }

    Optional<ExitCodeAndOutput> cmd_execute_and_capture_output(DiagnosticContext& context, const Command& cmd)
    {
        RedirectedProcessLaunchSettings default_redirected_process_launch_settings;
        return cmd_execute_and_capture_output(context, cmd, default_redirected_process_launch_settings);
    }

    Optional<ExitCodeAndOutput> cmd_execute_and_capture_output(DiagnosticContext& context,
                                                               const Command& cmd,
                                                               const RedirectedProcessLaunchSettings& settings)
    {
        std::string output;
        return cmd_execute_and_stream_data(context, cmd, settings, [&](StringView sv) { Strings::append(output, sv); })
            .map([&](ExitCodeIntegral exit_code) { return ExitCodeAndOutput{exit_code, std::move(output)}; });
    }

    uint64_t get_subproccess_stats() { return g_subprocess_stats.load(); }

#if defined(_WIN32)
    static BOOL ctrl_handler(DWORD fdw_ctrl_type)
    {
        switch (fdw_ctrl_type)
        {
            case CTRL_C_EVENT: g_ctrl_c_state.transition_handle_ctrl_c(); return TRUE;
            default: return FALSE;
        }
    }

    void register_console_ctrl_handler()
    {
        SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(ctrl_handler), TRUE);
    }
#else
    void register_console_ctrl_handler() { }
#endif

    bool succeeded(const ExpectedL<ExitCodeIntegral>& maybe_exit) noexcept
    {
        if (const auto exit = maybe_exit.get())
        {
            return *exit == 0;
        }

        return false;
    }

    ExpectedL<Unit> flatten(const ExpectedL<ExitCodeAndOutput>& maybe_exit, StringView tool_name)
    {
        if (auto exit = maybe_exit.get())
        {
            if (exit->exit_code == 0)
            {
                return {Unit{}};
            }

            return {msg::format_error(
                        msgProgramReturnedNonzeroExitCode, msg::tool_name = tool_name, msg::exit_code = exit->exit_code)
                        .append_raw('\n')
                        .append_raw(exit->output)};
        }

        return {msg::format_error(msgLaunchingProgramFailed, msg::tool_name = tool_name)
                    .append_raw(' ')
                    .append_raw(maybe_exit.error().to_string())};
    }

    ExpectedL<std::string> flatten_out(ExpectedL<ExitCodeAndOutput>&& maybe_exit, StringView tool_name)
    {
        if (auto exit = maybe_exit.get())
        {
            if (exit->exit_code == 0)
            {
                return {std::move(exit->output), expected_left_tag};
            }

            return {msg::format_error(
                        msgProgramReturnedNonzeroExitCode, msg::tool_name = tool_name, msg::exit_code = exit->exit_code)
                        .append_raw('\n')
                        .append_raw(exit->output),
                    expected_right_tag};
        }

        return {msg::format_error(msgLaunchingProgramFailed, msg::tool_name = tool_name)
                    .append_raw(' ')
                    .append_raw(maybe_exit.error().to_string()),
                expected_right_tag};
    }

    void replace_secrets(std::string& target, View<std::string> secrets)
    {
        const auto replacement = msg::format(msgSecretBanner);
        for (const auto& secret : secrets)
        {
            Strings::inplace_replace_all(target, secret, replacement);
        }
    }

    std::string* check_zero_exit_code(DiagnosticContext& context,
                                      const Command& command,
                                      Optional<ExitCodeAndOutput>& maybe_exit)
    {
        return check_zero_exit_code(context, command, maybe_exit, View<std::string>{});
    }

    std::string* check_zero_exit_code(DiagnosticContext& context,
                                      const Command& command,
                                      Optional<ExitCodeAndOutput>& maybe_exit,
                                      View<std::string> secrets)
    {
        if (auto exit = maybe_exit.get())
        {
            if (exit->exit_code == 0)
            {
                return &exit->output;
            }

            auto str_command = command.command_line().to_string();
            replace_secrets(str_command, secrets);
            context.report(DiagnosticLine{
                DiagKind::Error,
                LocalizedString::from_raw(str_command)
                    .append_raw(' ')
                    .append(msg::format(msgProgramPathReturnedNonzeroExitCode, msg::exit_code = exit->exit_code))
                    .append_raw('\n')
                    .append_raw(exit->output)});
        }

        return nullptr;
    }
} // namespace vcpkg
