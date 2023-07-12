#include <vcpkg/base/system-headers.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <ctime>
#include <future>
#include <set>

#if defined(__APPLE__)
extern char** environ;
#include <mach-o/dyld.h>
#endif

#if defined(__FreeBSD__)
extern char** environ;
#include <sys/sysctl.h>
#include <sys/wait.h>
#endif

#if defined(_WIN32)
#include <Psapi.h>
#include <TlHelp32.h>
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

#if defined(_WIN32)
    using error_value_type = unsigned long;
#else  // ^^^ _WIN32 // !_WIN32 vvv
    using error_value_type = int;
#endif // ^^^ !_WIN32

    LocalizedString format_system_error_message(StringLiteral api_name, error_value_type error_value)
    {
        return msg::format_error(msgSystemApiErrorMessage,
                                 msg::system_api = api_name,
                                 msg::exit_code = error_value,
                                 msg::error_msg = std::system_category().message(static_cast<int>(error_value)));
    }

    static std::atomic_int32_t debug_id_counter{1000};

    struct ChildStdinTracker
    {
        StringView input;
        std::size_t offset;

#if defined(_WIN32)
        // Write a hunk of data to `target`. If there is no more input to write, returns `true`.
        ExpectedL<bool> do_write(HANDLE target)
        {
            const auto this_write = input.size() - offset;
            if (this_write != 0)
            {
                const auto this_write_clamped = static_cast<DWORD>(this_write > MAXDWORD ? MAXDWORD : this_write);
                DWORD actually_written;
                if (!WriteFile(target,
                               static_cast<const void*>(input.data() + offset),
                               this_write_clamped,
                               &actually_written,
                               nullptr))
                {
                    return format_system_error_message("WriteFile", GetLastError());
                }

                offset += actually_written;
            }

            return offset == input.size();
        }
#else  // ^^^ _WIN32 // !_WIN32 vvv
       // Write a hunk of data to `target`. If there is no more input to write, returns `true`.
        ExpectedL<bool> do_write(int target)
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
                    return format_system_error_message("write", errno);
                }

                offset += actually_written;
            }

            return offset == input.size();
        }
#endif // ^^^ !_WIN32
    };
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
    namespace
    {
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
                    Checks::final_cleanup_and_exit(1);
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
                    Checks::final_cleanup_and_exit(1);
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
    }

    void initialize_global_job_object() { g_ctrl_c_state.initialize_job(); }
    void enter_interactive_subprocess() { g_ctrl_c_state.enter_interactive(); }
    void exit_interactive_subprocess() { g_ctrl_c_state.exit_interactive(); }
#endif

    Path get_exe_path_of_current_process()
    {
#if defined(_WIN32)
        wchar_t buf[_MAX_PATH];
        const int bytes = GetModuleFileNameW(nullptr, buf, _MAX_PATH);
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
        const char* progname = getprogname();
        char resolved_path[PATH_MAX];
        auto ret = realpath(progname, resolved_path);
        Checks::check_exit(VCPKG_LINE_INFO, ret != nullptr, "Could not determine current executable path.");
        return resolved_path;
#else /* LINUX */
        std::array<char, 1024 * 4> buf{};
        auto written = readlink("/proc/self/exe", buf.data(), buf.size());
        Checks::check_exit(VCPKG_LINE_INFO, written != -1, "Could not determine current executable path.");
        return Path(buf.data(), written);
#endif
    }

    Optional<ProcessStat> try_parse_process_stat_file(const FileContents& contents)
    {
        ParserBase p(contents.content, contents.origin);

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

        BOOL Process32First(PPROCESSENTRY32W entry) const noexcept { return Process32FirstW(snapshot, entry); }
        BOOL Process32Next(PPROCESSENTRY32W entry) const noexcept { return Process32NextW(snapshot, entry); }

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
                        "\\WindowsPowerShell\\v1.0\\");

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
            // Environment variables to tell git to use custom SSH executable or command
            "GIT_SSH",
            "GIT_SSH_COMMAND",
            // Points to a credential-manager binary for git authentication
            "GIT_ASKPASS",
            // Environment variables needed for ssh-agent based authentication
            "SSH_AUTH_SOCK",
            "SSH_AGENT_PID",
            // Enables find_package(CUDA) and enable_language(CUDA) in CMake
            "CUDA_PATH",
            "CUDA_PATH_V9_0",
            "CUDA_PATH_V9_1",
            "CUDA_PATH_V10_0",
            "CUDA_PATH_V10_1",
            "CUDA_PATH_V10_2",
            "CUDA_PATH_V11_0",
            "CUDA_PATH_V11_1",
            "CUDA_PATH_V11_2",
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

        const Optional<std::string> keep_vars = get_environment_variable("VCPKG_KEEP_ENV_VARS");
        const auto k = keep_vars.get();

        if (k && !k->empty())
        {
            auto vars = Strings::split(*k, ';');

            for (auto&& var : vars)
            {
                if (Strings::case_insensitive_ascii_equals(var, "PATH"))
                {
                    new_path.assign(prepend_to_path.data(), prepend_to_path.size());
                    if (!new_path.empty()) new_path.push_back(';');
                    new_path.append(get_environment_variable("PATH").value_or(""));
                }
                else
                {
                    env_strings.push_back(var);
                }
            }
        }

        Environment env;

        for (auto&& env_string : env_strings)
        {
            const Optional<std::string> value = get_environment_variable(env_string.c_str());
            const auto v = value.get();
            if (!v || v->empty()) continue;

            env.add_entry(env_string, *v);
        }

        if (extra_env.find("PATH") != extra_env.end())
        {
            new_path.push_back(';');
            new_path += extra_env.find("PATH")->second;
        }
        env.add_entry("PATH", new_path);
        // NOTE: we support VS's without the english language pack,
        // but we still want to default to english just in case your specific
        // non-standard build system doesn't support non-english
        env.add_entry("VSLANG", "1033");
        env.add_entry("VSCMD_SKIP_SENDTELEMETRY", "1");

        for (const auto& item : extra_env)
        {
            if (item.first == "PATH") continue;
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
                "PATH",
                Strings::concat(prepend_to_path, ':', get_environment_variable("PATH").value_or_exit(VCPKG_LINE_INFO)));
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

    const WorkingDirectory default_working_directory;
    const Environment default_environment;

    std::vector<ExpectedL<ExitCodeAndOutput>> cmd_execute_and_capture_output_parallel(View<Command> cmd_lines,
                                                                                      const WorkingDirectory& wd,
                                                                                      const Environment& env)
    {
        std::vector<ExpectedL<ExitCodeAndOutput>> res(cmd_lines.size(), LocalizedString{});
        if (cmd_lines.empty())
        {
            return res;
        }

        if (cmd_lines.size() == 1)
        {
            res[0] = cmd_execute_and_capture_output(cmd_lines[0], wd, env);
            return res;
        }

        std::atomic<size_t> work_item{0};
        const auto num_threads =
            std::max(static_cast<size_t>(1), std::min(static_cast<size_t>(get_concurrency()), cmd_lines.size()));

        auto work = [&]() {
            std::size_t item;
            while (item = work_item.fetch_add(1), item < cmd_lines.size())
            {
                res[item] = cmd_execute_and_capture_output(cmd_lines[item], wd, env);
            }
        };

        std::vector<std::future<void>> workers;
        workers.reserve(num_threads - 1);
        for (size_t x = 0; x < num_threads - 1; ++x)
        {
            workers.emplace_back(std::async(std::launch::async | std::launch::deferred, work));
            if (work_item >= cmd_lines.size())
            {
                break;
            }
        }
        work();
        for (auto&& w : workers)
        {
            w.get();
        }
        return res;
    }

    ExpectedL<int> cmd_execute_clean(const Command& cmd_line, const WorkingDirectory& wd)
    {
        return cmd_execute(cmd_line, wd, get_clean_environment());
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

    struct ProcessInfo
    {
        ProcessInfo() noexcept : proc_info{}
        {
            proc_info.hProcess = INVALID_HANDLE_VALUE;
            proc_info.hThread = INVALID_HANDLE_VALUE;
        }

        ProcessInfo(ProcessInfo&& other) noexcept : proc_info(other.proc_info)
        {
            other.proc_info.hProcess = INVALID_HANDLE_VALUE;
            other.proc_info.hThread = INVALID_HANDLE_VALUE;
        }

        ~ProcessInfo()
        {
            close_handle_mark_invalid(proc_info.hThread);
            close_handle_mark_invalid(proc_info.hProcess);
        }

        ProcessInfo& operator=(ProcessInfo&& other) noexcept
        {
            ProcessInfo{std::move(other)}.swap(*this);
            return *this;
        }

        void swap(ProcessInfo& other) noexcept
        {
            std::swap(proc_info.hProcess, other.proc_info.hProcess);
            std::swap(proc_info.hThread, other.proc_info.hThread);
        }

        friend void swap(ProcessInfo& lhs, ProcessInfo& rhs) noexcept { lhs.swap(rhs); }

        unsigned int wait()
        {
            close_handle_mark_invalid(proc_info.hThread);
            const DWORD result = WaitForSingleObject(proc_info.hProcess, INFINITE);
            Checks::check_exit(VCPKG_LINE_INFO, result != WAIT_FAILED, "WaitForSingleObject failed");
            DWORD exit_code = 0;
            GetExitCodeProcess(proc_info.hProcess, &exit_code);
            close_handle_mark_invalid(proc_info.hProcess);
            return exit_code;
        }

        PROCESS_INFORMATION proc_info;
    };

    /// <param name="maybe_environment">If non-null, an environment block to use for the new process. If null, the
    /// new process will inherit the current environment.</param>
    ExpectedL<ProcessInfo> windows_create_process(std::int32_t debug_id,
                                                  StringView cmd_line,
                                                  const WorkingDirectory& wd,
                                                  const Environment& env,
                                                  DWORD dwCreationFlags,
                                                  STARTUPINFOEXW& startup_info) noexcept
    {
        ProcessInfo process_info;
        Debug::print(fmt::format("{}: CreateProcessW({})\n", debug_id, cmd_line));

        // Flush stdout before launching external process
        fflush(nullptr);

        std::wstring working_directory;
        if (!wd.working_directory.empty())
        {
            // this only fails if we can't get the current working directory of vcpkg, and we assume that we have that,
            // so it's fine anyways
            working_directory = Strings::to_utf16(real_filesystem.absolute(wd.working_directory, VCPKG_LINE_INFO));
        }

        auto&& env_unpacked = env.get();
        std::wstring environment_block;
        LPVOID call_environment = nullptr;
        if (!env_unpacked.empty())
        {
            environment_block = env_unpacked;
            environment_block.push_back('\0');
            call_environment = environment_block.data();
        }

        // Leaking process information handle 'process_info.proc_info.hProcess'
        // /analyze can't tell that we transferred ownership here
        VCPKG_MSVC_WARNING(suppress : 6335)
        if (CreateProcessW(nullptr,
                           Strings::to_utf16(cmd_line).data(),
                           nullptr,
                           nullptr,
                           TRUE,
                           IDLE_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT |
                               dwCreationFlags,
                           call_environment,
                           working_directory.empty() ? nullptr : working_directory.data(),
                           &startup_info.StartupInfo,
                           &process_info.proc_info))
        {
            return process_info;
        }

        return format_system_error_message("CreateProcessW", GetLastError());
    }

    ExpectedL<ProcessInfo> windows_create_windowless_process(std::int32_t debug_id,
                                                             StringView cmd_line,
                                                             const WorkingDirectory& wd,
                                                             const Environment& env,
                                                             DWORD dwCreationFlags) noexcept
    {
        STARTUPINFOEXW startup_info_ex;
        memset(&startup_info_ex, 0, sizeof(STARTUPINFOEXW));
        startup_info_ex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        startup_info_ex.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startup_info_ex.StartupInfo.wShowWindow = SW_HIDE;

        return windows_create_process(debug_id, cmd_line, wd, env, dwCreationFlags, startup_info_ex);
    }

    struct ProcessInfoAndPipes
    {
        ProcessInfo proc_info;
        HANDLE child_handles[2]{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE}; // [0] == child_stdin, [1] == child_stdout

        ProcessInfoAndPipes() = default;
        ProcessInfoAndPipes(const ProcessInfoAndPipes&) = delete;
        ProcessInfoAndPipes(ProcessInfoAndPipes&& other) noexcept : proc_info(std::move(other.proc_info))
        {
            child_handles[0] = std::exchange(other.child_handles[0], INVALID_HANDLE_VALUE);
            child_handles[1] = std::exchange(other.child_handles[1], INVALID_HANDLE_VALUE);
        }

        ProcessInfoAndPipes& operator=(const ProcessInfoAndPipes&) = delete;
        ~ProcessInfoAndPipes()
        {
            close_handle_mark_invalid(child_handles[0]);
            close_handle_mark_invalid(child_handles[1]);
        }

        void handle_child_read(ChildStdinTracker& stdin_tracker)
        {
            auto maybe_done = stdin_tracker.do_write(child_handles[0]);
            if (auto done = maybe_done.get())
            {
                if (*done)
                {
                    close_handle_mark_invalid(child_handles[0]);
                }

                return;
            }

            vcpkg::Checks::unreachable(VCPKG_LINE_INFO);
        }

        template<class Function>
        void handle_child_write(const Function& f, char* buf, DWORD buffer_size, Encoding encoding)
        {
            DWORD bytes_read = 0;
            if (!ReadFile(child_handles[1], static_cast<void*>(buf), buffer_size, &bytes_read, nullptr))
            {
                if (GetLastError() != ERROR_BROKEN_PIPE)
                {
                    vcpkg::Checks::unreachable(VCPKG_LINE_INFO);
                }

                close_handle_mark_invalid(child_handles[1]);
                return;
            }

            if (encoding == Encoding::Utf8)
            {
                std::replace(buf, buf + bytes_read, '\0', '?');
                f(StringView{buf, static_cast<size_t>(bytes_read)});
            }
            else if (encoding == Encoding::Utf16)
            {
                // Note: This doesn't handle unpaired surrogates or partial encoding units correctly in
                // order to be able to reuse Strings::to_utf8 which we believe will be fine 99% of the time.
                std::string encoded;
                Strings::to_utf8(encoded, reinterpret_cast<const wchar_t*>(buf), bytes_read);
                std::replace(encoded.begin(), encoded.end(), '\0', '?');
                f(StringView{encoded});
            }
            else
            {
                vcpkg::Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        template<class Function>
        int wait_and_stream_output(StringView input, const Function& f, Encoding encoding)
        {
            ChildStdinTracker input_tracker{input, 0};
            static constexpr DWORD buffer_size = 1024 * 32;
            char buf[buffer_size];
            while (child_handles[0] != INVALID_HANDLE_VALUE && child_handles[1] != INVALID_HANDLE_VALUE)
            {
                switch (WaitForMultipleObjects(2, child_handles, FALSE, INFINITE))
                {
                    case WAIT_OBJECT_0: handle_child_read(input_tracker); break;
                    case WAIT_OBJECT_0 + 1: handle_child_write(f, buf, buffer_size, encoding); break;
                    default: vcpkg::Checks::unreachable(VCPKG_LINE_INFO); break;
                }
            }

            while (child_handles[0] != INVALID_HANDLE_VALUE &&
                   WaitForSingleObject(child_handles[0], INFINITE) == WAIT_OBJECT_0)
            {
                handle_child_read(input_tracker);
            }

            while (child_handles[1] != INVALID_HANDLE_VALUE &&
                   WaitForSingleObject(child_handles[1], INFINITE) == WAIT_OBJECT_0)
            {
                handle_child_write(f, buf, buffer_size, encoding);
            }

            return proc_info.wait();
        }
    };

    struct ProcAttributeList
    {
        static ExpectedL<ProcAttributeList> create(DWORD dwAttributeCount)
        {
            SIZE_T size = 0;
            if (InitializeProcThreadAttributeList(nullptr, dwAttributeCount, 0, &size) ||
                GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            {
                return format_system_error_message("InitializeProcThreadAttributeList nullptr", GetLastError());
            }
            Checks::check_exit(VCPKG_LINE_INFO, size > 0);
            ASSUME(size > 0);
            std::vector<unsigned char> buffer(size, 0);
            if (!InitializeProcThreadAttributeList(
                    reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data()), dwAttributeCount, 0, &size))
            {
                return format_system_error_message("InitializeProcThreadAttributeList attribute_list", GetLastError());
            }
            return ProcAttributeList(std::move(buffer));
        }
        ExpectedL<Unit> update_attribute(DWORD_PTR Attribute, PVOID lpValue, SIZE_T cbSize)
        {
            if (!UpdateProcThreadAttribute(get(), 0, Attribute, lpValue, cbSize, nullptr, nullptr))
            {
                return format_system_error_message("InitializeProcThreadAttributeList attribute_list", GetLastError());
            }
            return Unit{};
        }
        ~ProcAttributeList() { DeleteProcThreadAttributeList(get()); }
        LPPROC_THREAD_ATTRIBUTE_LIST get() noexcept
        {
            return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        }

        ProcAttributeList(const ProcAttributeList&) = delete;
        ProcAttributeList& operator=(const ProcAttributeList&) = delete;
        ProcAttributeList(ProcAttributeList&&) = default;
        ProcAttributeList& operator=(ProcAttributeList&&) = default;

    private:
        explicit ProcAttributeList(std::vector<unsigned char>&& buffer) : buffer(std::move(buffer)) { }
        std::vector<unsigned char> buffer;
    };

    struct StartupInfoWithPipes : STARTUPINFOEXW
    {
        StartupInfoWithPipes() noexcept : STARTUPINFOEXW{}
        {
            StartupInfo.cb = sizeof(STARTUPINFOEXW);
            StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
            StartupInfo.hStdOutput = INVALID_HANDLE_VALUE;
            StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        }

        StartupInfoWithPipes(const StartupInfoWithPipes&) = delete;
        StartupInfoWithPipes& operator=(const StartupInfoWithPipes&) = delete;

        ~StartupInfoWithPipes()
        {
            close_handle_mark_invalid(StartupInfo.hStdInput);
            close_handle_mark_invalid(StartupInfo.hStdOutput);
        }
    };

    ExpectedL<ProcessInfoAndPipes> windows_create_process_redirect(std::int32_t debug_id,
                                                                   StringView cmd_line,
                                                                   const WorkingDirectory& wd,
                                                                   const Environment& env,
                                                                   DWORD dwCreationFlags) noexcept
    {
        ProcessInfoAndPipes ret;
        StartupInfoWithPipes startup_info_ex;
        SECURITY_ATTRIBUTES saAttr{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

        // Create a pipe for the child process's STDOUT.
        if (!CreatePipe(&ret.child_handles[1], &startup_info_ex.StartupInfo.hStdOutput, &saAttr, 0))
        {
            return format_system_error_message("CreatePipe stdout", GetLastError());
        }

        // Create a pipe for the child process's STDIN.
        if (!CreatePipe(&startup_info_ex.StartupInfo.hStdInput, &ret.child_handles[0], &saAttr, 0))
        {
            return format_system_error_message("CreatePipe stdin", GetLastError());
        }

        DWORD nonblocking_flags = PIPE_NOWAIT;
        if (!SetNamedPipeHandleState(ret.child_handles[0], &nonblocking_flags, nullptr, nullptr))
        {
            return format_system_error_message("SetNamedPipeHandleState", GetLastError());
        }

        startup_info_ex.StartupInfo.hStdError = startup_info_ex.StartupInfo.hStdOutput;

        // Ensure that only the write handle to STDOUT and the read handle to STDIN are inherited.
        // from https://devblogs.microsoft.com/oldnewthing/20111216-00/?p=8873
        ExpectedL<ProcAttributeList> proc_attribute_list = ProcAttributeList::create(1);
        if (!proc_attribute_list.has_value())
        {
            return proc_attribute_list.error();
        }

        HANDLE handles_to_inherit[2] = {startup_info_ex.StartupInfo.hStdOutput, startup_info_ex.StartupInfo.hStdInput};
        auto maybe_error = proc_attribute_list.get()->update_attribute(
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles_to_inherit, 2 * sizeof(HANDLE));
        if (!maybe_error.has_value())
        {
            return maybe_error.error();
        }
        startup_info_ex.lpAttributeList = proc_attribute_list.get()->get();

        auto maybe_proc_info = windows_create_process(debug_id, cmd_line, wd, env, dwCreationFlags, startup_info_ex);

        if (auto proc_info = maybe_proc_info.get())
        {
            ret.proc_info = std::move(*proc_info);
            return ret;
        }

        return maybe_proc_info.error();
    }
#else // ^^^ _WIN32 // !_WIN32 vvv
    struct AnonymousPipe
    {
        // pipefd[0] is the read end of the pipe, pipefd[1] is the write end
        int pipefd[2];
        AnonymousPipe() : pipefd{-1, -1} { }
        AnonymousPipe(const AnonymousPipe&) = delete;
        AnonymousPipe(AnonymousPipe&& other)
        {
            for (size_t idx = 0; idx < 2; ++idx)
            {
                pipefd[idx] = std::exchange(other.pipefd[idx], -1);
            }
        }
        ~AnonymousPipe()
        {
            for (size_t idx = 0; idx < 2; ++idx)
            {
                close_mark_invalid(pipefd[idx]);
            }
        }

        friend void swap(AnonymousPipe& lhs, AnonymousPipe& rhs) noexcept
        {
            for (size_t idx = 0; idx < 2; ++idx)
            {
                std::swap(lhs.pipefd[idx], rhs.pipefd[idx]);
            }
        }

        AnonymousPipe& operator=(const AnonymousPipe&) = delete;
        AnonymousPipe& operator=(AnonymousPipe&& other)
        {
            auto moved = std::move(other);
            swap(*this, moved);
            return *this;
        }

        ExpectedL<Unit> create()
        {
#if defined(__APPLE__)
            static std::mutex pipe_creation_lock;
            std::lock_guard<std::mutex> lck{pipe_creation_lock};
            if (pipe(pipefd))
            {
                return format_system_error_message("pipe", errno);
            }

            for (size_t idx = 0; idx < 2; ++idx)
            {
                if (fcntl(pipefd[idx], F_SETFD, FD_CLOEXEC))
                {
                    return format_system_error_message("fcntl", errno);
                }
            }
#else  // ^^^ Apple // !Apple vvv
            if (pipe2(pipefd, O_CLOEXEC))
            {
                return format_system_error_message("pipe2", errno);
            }
#endif // ^^^ !Apple

            return Unit{};
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

        ExpectedL<Unit> adddup2(int fd, int newfd)
        {
            const int error = posix_spawn_file_actions_adddup2(&actions, fd, newfd);
            if (error)
            {
                return format_system_error_message("posix_spawn_file_actions_adddup2", error);
            }

            return Unit{};
        }
    };

    struct PosixPid
    {
        pid_t pid;

        PosixPid() : pid{-1} { }

        ExpectedL<int> wait_for_termination()
        {
            int exit_code;
            if (pid != -1)
            {
                int status;
                const auto child = waitpid(pid, &status, 0);
                if (child != pid)
                {
                    return format_system_error_message("waitpid", errno);
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
    Environment cmd_execute_and_capture_environment(const Command& cmd_line, const Environment& env)
    {
        static StringLiteral magic_string = "cdARN4xjKueKScMy9C6H";

        auto actual_cmd_line = cmd_line;
        actual_cmd_line.raw_arg(Strings::concat(" & echo ", magic_string, " & set"));

        Debug::print("command line: ", actual_cmd_line.command_line(), "\n");
        auto maybe_rc_output = cmd_execute_and_capture_output(actual_cmd_line, default_working_directory, env);
        if (!maybe_rc_output)
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgVcvarsRunFailed);
        }

        auto& rc_output = maybe_rc_output.value_or_exit(VCPKG_LINE_INFO);
        if (rc_output.exit_code != 0)
        {
            Checks::msg_exit_with_error(
                VCPKG_LINE_INFO, msgVcvarsRunFailedExitCode, msg::exit_code = rc_output.exit_code);
        }

        Debug::print(rc_output.output, "\n");

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
        auto process_info =
            windows_create_windowless_process(debug_id,
                                              cmd_line.command_line(),
                                              default_working_directory,
                                              default_environment,
                                              CREATE_NEW_CONSOLE | CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB);
        if (!process_info)
        {
            Debug::print(fmt::format("{}: cmd_execute_background() failed: {}\n", debug_id, process_info.error()));
        }
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
        if (error)
        {
            Debug::print(fmt::format("{}: cmd_execute_background() failed: {}\n", debug_id, error));
        }
#endif // ^^^ !_WIN32
    }

    static ExpectedL<int> cmd_execute_impl(const int32_t debug_id,
                                           const Command& cmd_line,
                                           const WorkingDirectory& wd,
                                           const Environment& env)
    {
#if defined(_WIN32)
        using vcpkg::g_ctrl_c_state;
        g_ctrl_c_state.transition_to_spawn_process();
        auto result = windows_create_windowless_process(debug_id, cmd_line.command_line(), wd, env, 0)
                          .map([](ProcessInfo&& proc_info) {
                              auto long_exit_code = proc_info.wait();
                              if (long_exit_code > INT_MAX) long_exit_code = INT_MAX;
                              return static_cast<int>(long_exit_code);
                          });
        g_ctrl_c_state.transition_from_spawn_process();
        return result;
#else
        (void)env;
        Command real_command_line_builder;
        if (!wd.working_directory.empty())
        {
            real_command_line_builder.string_arg("cd");
            real_command_line_builder.string_arg(wd.working_directory);
            real_command_line_builder.raw_arg("&&");
        }

        if (!env.get().empty())
        {
            real_command_line_builder.raw_arg(env.get());
        }

        real_command_line_builder.raw_arg(cmd_line.command_line());

        std::string real_command_line = std::move(real_command_line_builder).extract();
        Debug::print(fmt::format("{}: system({})\n", debug_id, real_command_line));
        fflush(nullptr);

        return system(real_command_line.c_str());
#endif
    }

    ExpectedL<int> cmd_execute(const Command& cmd_line, const WorkingDirectory& wd, const Environment& env)
    {
        const ElapsedTimer timer;
        const auto debug_id = debug_id_counter.fetch_add(1, std::memory_order_relaxed);
        auto maybe_result = cmd_execute_impl(debug_id, cmd_line, wd, env);
        const auto elapsed = timer.us_64();
        g_subprocess_stats += elapsed;
        if (auto result = maybe_result.get())
        {
            Debug::print(fmt::format("{}: cmd_execute() returned {} after {} us\n", debug_id, *result, elapsed));
        }
        else
        {
            Debug::print(
                fmt::format("{}: cmd_execute() returned ({}) after {} us\n", debug_id, maybe_result.error(), elapsed));
        }

        return maybe_result;
    }

    ExpectedL<int> cmd_execute_and_stream_lines(const Command& cmd_line,
                                                std::function<void(StringView)> per_line_cb,
                                                const WorkingDirectory& wd,
                                                const Environment& env,
                                                Encoding encoding,
                                                StringView stdin_content)
    {
        Strings::LinesStream lines;

        auto rc = cmd_execute_and_stream_data(
            cmd_line, [&](const StringView sv) { lines.on_data(sv, per_line_cb); }, wd, env, encoding, stdin_content);
        lines.on_end(per_line_cb);
        return rc;
    }

    ExpectedL<int> cmd_execute_and_stream_data(const Command& cmd_line,
                                               std::function<void(StringView)> data_cb,
                                               const WorkingDirectory& wd,
                                               const Environment& env,
                                               Encoding encoding,
                                               StringView stdin_content)
    {
        const ElapsedTimer timer;
        const auto debug_id = debug_id_counter.fetch_add(1, std::memory_order_relaxed);
#if defined(_WIN32)
        using vcpkg::g_ctrl_c_state;

        g_ctrl_c_state.transition_to_spawn_process();
        ExpectedL<int> exit_code =
            windows_create_process_redirect(debug_id, cmd_line.command_line(), wd, env, 0)
                .map([&](ProcessInfoAndPipes&& output) {
                    if (encoding == Encoding::Utf16)
                    {
                        auto as_utf16 = Strings::to_utf16(stdin_content);
                        return output.wait_and_stream_output(StringView{reinterpret_cast<const char*>(as_utf16.data()),
                                                                        as_utf16.size() * sizeof(wchar_t)},
                                                             data_cb,
                                                             encoding);
                    }

                    return output.wait_and_stream_output(stdin_content, data_cb, encoding);
                });
        g_ctrl_c_state.transition_from_spawn_process();
#else  // ^^^ _WIN32 // !_WIN32 vvv

        Checks::check_exit(VCPKG_LINE_INFO, encoding == Encoding::Utf8);
        std::string actual_cmd_line;
        if (!wd.working_directory.empty())
        {
            actual_cmd_line.append("cd ");
            append_shell_escaped(actual_cmd_line, wd.working_directory);
            actual_cmd_line.append(" && ");
        }

        const auto& env_text = env.get();
        if (!env_text.empty())
        {
            actual_cmd_line.append(env_text);
            actual_cmd_line.push_back(' ');
        }

        const auto unwrapped_to_execute = cmd_line.command_line();
        actual_cmd_line.append(unwrapped_to_execute.data(), unwrapped_to_execute.size());

        Debug::print(fmt::format("{}: execute_process({})\n", debug_id, actual_cmd_line));
        // Flush stdout before launching external process
        fflush(stdout);

        AnonymousPipe child_input;
        {
            auto err = child_input.create();
            if (!err)
            {
                return std::move(err).error();
            }
        }

        AnonymousPipe child_output;
        {
            auto err = child_output.create();
            if (!err)
            {
                return std::move(err).error();
            }
        }

        PosixSpawnFileActions actions;
        actions.adddup2(child_input.pipefd[0], 0);
        actions.adddup2(child_output.pipefd[1], 1);
        actions.adddup2(child_output.pipefd[1], 2);

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
            return format_system_error_message("posix_spawn", error);
        }

        close_mark_invalid(child_input.pipefd[0]);
        close_mark_invalid(child_output.pipefd[1]);

        char buf[1024];
        ChildStdinTracker stdin_tracker{stdin_content, 0};
        if (stdin_content.empty())
        {
            close_mark_invalid(child_input.pipefd[1]);
        }
        else
        {
            if (fcntl(child_input.pipefd[1], F_SETFL, O_NONBLOCK))
            {
                return format_system_error_message("fcntl", errno);
            }

            auto maybe_done = stdin_tracker.do_write(child_input.pipefd[1]);
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
                return std::move(maybe_done).error();
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
                        return format_system_error_message("poll", errno);
                    }

                    if (polls[0].revents & POLLERR)
                    {
                        close_mark_invalid(child_input.pipefd[1]);
                        break;
                    }
                    else if (polls[0].revents & POLLOUT)
                    {
                        auto maybe_next_done = stdin_tracker.do_write(child_input.pipefd[1]);
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
                            return std::move(maybe_next_done).error();
                        }
                    }

                    if (polls[1].revents & POLLIN)
                    {
                        auto read_amount = read(child_output.pipefd[0], buf, sizeof(buf));
                        if (read_amount < 0)
                        {
                            return format_system_error_message("read", errno);
                        }

                        // can't be 0 because poll told us otherwise
                        if (read_amount == 0)
                        {
                            Checks::unreachable(VCPKG_LINE_INFO);
                        }

                        data_cb(StringView{buf, static_cast<size_t>(read_amount)});
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

                return format_system_error_message("read", error);
            }

            if (read_amount == 0)
            {
                close_mark_invalid(child_output.pipefd[0]);
                break;
            }

            data_cb(StringView{buf, static_cast<size_t>(read_amount)});
        }

        ExpectedL<int> exit_code = pid.wait_for_termination();
#endif /// ^^^ !_WIN32

        const auto elapsed = timer.us_64();
        g_subprocess_stats += elapsed;
        if (const auto pec = exit_code.get())
        {
            Debug::print(fmt::format("{}: cmd_execute_and_stream_data() returned {} after {:8} us\n",
                                     debug_id,
                                     *pec,
                                     static_cast<unsigned long long>(elapsed)));
        }

        return exit_code;
    }

    ExpectedL<ExitCodeAndOutput> cmd_execute_and_capture_output(const Command& cmd_line,
                                                                const WorkingDirectory& wd,
                                                                const Environment& env,
                                                                Encoding encoding,
                                                                EchoInDebug echo_in_debug,
                                                                StringView stdin_content)
    {
        std::string output;
        return cmd_execute_and_stream_data(
                   cmd_line,
                   [&](StringView sv) {
                       Strings::append(output, sv);
                       if (echo_in_debug == EchoInDebug::Show && Debug::g_debugging)
                       {
                           msg::write_unlocalized_text_to_stdout(Color::none, sv);
                       }
                   },
                   wd,
                   env,
                   encoding,
                   stdin_content)
            .map([&](int exit_code) {
                return ExitCodeAndOutput{exit_code, std::move(output)};
            });
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

    bool succeeded(const ExpectedL<int>& maybe_exit) noexcept
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

} // namespace vcpkg
