#include <vcpkg/base/system-headers.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <ctime>
#include <future>

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
// #include <winternl.h>
#include <TlHelp32.h>
#pragma comment(lib, "Advapi32")
// #pragma comment(lib, "Ntdll")
#else
#include <spawn.h>
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

    void get_parent_process_list(std::vector<std::string>& ret)
    {
#if defined(_WIN32)
        // Enumerate all processes in the system snapshot.
        auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;

        std::map<DWORD, DWORD> pid_ppid_map;
        std::map<DWORD, std::string> pid_exe_path_map;

        PROCESSENTRY32W entry;
        memset(&entry, 0, sizeof(entry));
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                pid_ppid_map.emplace(entry.th32ProcessID, entry.th32ParentProcessID);
                pid_exe_path_map.emplace(entry.th32ProcessID, Strings::to_utf8(entry.szExeFile));
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);

        // Find hierarchy of current process
        auto it = pid_ppid_map.find(GetCurrentProcessId());
        if (it != pid_ppid_map.end())
        {
            // skip the vcpkg process
            it = pid_ppid_map.find(it->second);
            while (it != pid_ppid_map.end())
            {
                auto name = pid_exe_path_map[it->first];
                ret.push_back(name);
                it = pid_ppid_map.find(it->second);
            }
        }
#else
        (void)ret;
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

#if defined(_WIN32)
    struct ProcessInfo
    {
        constexpr ProcessInfo() noexcept : proc_info{} { }
        ProcessInfo(ProcessInfo&& other) noexcept : proc_info(other.proc_info)
        {
            other.proc_info.hProcess = nullptr;
            other.proc_info.hThread = nullptr;
        }
        ~ProcessInfo()
        {
            if (proc_info.hThread)
            {
                CloseHandle(proc_info.hThread);
            }
            if (proc_info.hProcess)
            {
                CloseHandle(proc_info.hProcess);
            }
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
            const DWORD result = WaitForSingleObject(proc_info.hProcess, INFINITE);
            Checks::check_exit(VCPKG_LINE_INFO, result != WAIT_FAILED, "WaitForSingleObject failed");
            DWORD exit_code = 0;
            GetExitCodeProcess(proc_info.hProcess, &exit_code);
            return exit_code;
        }

        PROCESS_INFORMATION proc_info;
    };

    /// <param name="maybe_environment">If non-null, an environment block to use for the new process. If null, the
    /// new process will inherit the current environment.</param>
    static ExpectedL<ProcessInfo> windows_create_process(StringView cmd_line,
                                                         const WorkingDirectory& wd,
                                                         const Environment& env,
                                                         DWORD dwCreationFlags,
                                                         STARTUPINFOEXW& startup_info) noexcept
    {
        ProcessInfo process_info;
        Debug::print("CreateProcessW(", cmd_line, ")\n");

        // Flush stdout before launching external process
        fflush(nullptr);

        std::wstring working_directory;
        if (!wd.working_directory.empty())
        {
            // this only fails if we can't get the current working directory of vcpkg, and we assume that we have that,
            // so it's fine anyways
            working_directory =
                Strings::to_utf16(get_real_filesystem().absolute(wd.working_directory, VCPKG_LINE_INFO));
        }

        auto environment_block = env.get();
        environment_block.push_back('\0');
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
                           env.get().empty() ? nullptr : environment_block.data(),
                           working_directory.empty() ? nullptr : working_directory.data(),
                           &startup_info.StartupInfo,
                           &process_info.proc_info))
        {
            return process_info;
        }

        return format_system_error_message("CreateProcessW", GetLastError());
    }

    static ExpectedL<ProcessInfo> windows_create_windowless_process(StringView cmd_line,
                                                                    const WorkingDirectory& wd,
                                                                    const Environment& env,
                                                                    DWORD dwCreationFlags) noexcept
    {
        STARTUPINFOEXW startup_info_ex;
        memset(&startup_info_ex, 0, sizeof(STARTUPINFOEXW));
        startup_info_ex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        startup_info_ex.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startup_info_ex.StartupInfo.wShowWindow = SW_HIDE;

        return windows_create_process(cmd_line, wd, env, dwCreationFlags, startup_info_ex);
    }

    struct ProcessInfoAndPipes
    {
        ProcessInfo proc_info;
        HANDLE child_stdin = 0;
        HANDLE child_stdout = 0;

        template<class Function>
        int wait_and_stream_output(Function&& f, Encoding encoding)
        {
            CloseHandle(child_stdin);

            DWORD bytes_read = 0;
            static constexpr DWORD buffer_size = 1024 * 32;
            char buf[buffer_size];
            if (encoding == Encoding::Utf8)
            {
                while (ReadFile(child_stdout, static_cast<void*>(buf), buffer_size, &bytes_read, nullptr))
                {
                    std::replace(buf, buf + bytes_read, '\0', '?');
                    f(StringView{buf, static_cast<size_t>(bytes_read)});
                }
            }
            else if (encoding == Encoding::Utf16)
            {
                // Note: This doesn't handle unpaired surrogates or partial encoding units correctly in order
                // to be able to reuse Strings::to_utf8 which we believe will be fine 99% of the time.
                std::string encoded;
                while (ReadFile(child_stdout, static_cast<void*>(buf), buffer_size, &bytes_read, nullptr))
                {
                    Strings::to_utf8(encoded, reinterpret_cast<const wchar_t*>(buf), bytes_read);
                    f(StringView{encoded});
                }
            }
            else
            {
                vcpkg::Checks::unreachable(VCPKG_LINE_INFO);
            }

            Debug::print(fmt::format("ReadFile() finished with GetLastError(): {}\n", GetLastError()));
            CloseHandle(child_stdout);
            return proc_info.wait();
        }
    };

    static ExpectedL<ProcessInfoAndPipes> windows_create_process_redirect(StringView cmd_line,
                                                                          const WorkingDirectory& wd,
                                                                          const Environment& env,
                                                                          DWORD dwCreationFlags) noexcept
    {
        ProcessInfoAndPipes ret;

        STARTUPINFOEXW startup_info_ex;
        memset(&startup_info_ex, 0, sizeof(STARTUPINFOEXW));
        startup_info_ex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        startup_info_ex.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;

        SECURITY_ATTRIBUTES saAttr;
        memset(&saAttr, 0, sizeof(SECURITY_ATTRIBUTES));
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        // Create a pipe for the child process's STDOUT.
        if (!CreatePipe(&ret.child_stdout, &startup_info_ex.StartupInfo.hStdOutput, &saAttr, 0))
        {
            return format_system_error_message("CreatePipe stdout", GetLastError());
        }

        // Create a pipe for the child process's STDIN.
        if (!CreatePipe(&startup_info_ex.StartupInfo.hStdInput, &ret.child_stdin, &saAttr, 0))
        {
            return format_system_error_message("CreatePipe stdin", GetLastError());
        }

        startup_info_ex.StartupInfo.hStdError = startup_info_ex.StartupInfo.hStdOutput;

        // Ensure that only the write handle to STDOUT and the read handle to STDIN are inherited.
        // from https://devblogs.microsoft.com/oldnewthing/20111216-00/?p=8873
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
                    return format_system_error_message("InitializeProcThreadAttributeList attribute_list",
                                                       GetLastError());
                }
                return ProcAttributeList(std::move(buffer));
            }
            ExpectedL<Unit> update_attribute(DWORD_PTR Attribute, PVOID lpValue, SIZE_T cbSize)
            {
                if (!UpdateProcThreadAttribute(get(), 0, Attribute, lpValue, cbSize, nullptr, nullptr))
                {
                    return format_system_error_message("InitializeProcThreadAttributeList attribute_list",
                                                       GetLastError());
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

        ExpectedL<ProcAttributeList> proc_attribute_list = ProcAttributeList::create(1);
        if (!proc_attribute_list.has_value())
        {
            return proc_attribute_list.error();
        }
        std::vector<HANDLE> handles_to_inherit = {
            {startup_info_ex.StartupInfo.hStdOutput, startup_info_ex.StartupInfo.hStdInput}};
        auto maybe_error = proc_attribute_list.get()->update_attribute(
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles_to_inherit.data(), handles_to_inherit.size() * sizeof(HANDLE));
        if (!maybe_error.has_value())
        {
            return maybe_error.error();
        }
        startup_info_ex.lpAttributeList = proc_attribute_list.get()->get();

        auto maybe_proc_info = windows_create_process(cmd_line, wd, env, dwCreationFlags, startup_info_ex);

        CloseHandle(startup_info_ex.StartupInfo.hStdInput);
        CloseHandle(startup_info_ex.StartupInfo.hStdOutput);

        if (auto proc_info = maybe_proc_info.get())
        {
            ret.proc_info = std::move(*proc_info);
            return ret;
        }

        return maybe_proc_info.error();
    }
#endif

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
        Debug::println("cmd_execute_background: ", cmd_line.command_line());
#if defined(_WIN32)
        auto process_info =
            windows_create_windowless_process(cmd_line.command_line(),
                                              default_working_directory,
                                              default_environment,
                                              CREATE_NEW_CONSOLE | CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB);
        if (!process_info)
        {
            Debug::println("cmd_execute_background() failed: ", process_info.error());
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
            Debug::println(fmt::format("cmd_execute_background() failed: {}", error));
        }
#endif // ^^^ !_WIN32
    }

    static ExpectedL<int> cmd_execute_impl(const Command& cmd_line, const WorkingDirectory& wd, const Environment& env)
    {
#if defined(_WIN32)
        using vcpkg::g_ctrl_c_state;
        g_ctrl_c_state.transition_to_spawn_process();
        auto result =
            windows_create_windowless_process(cmd_line.command_line(), wd, env, 0).map([](ProcessInfo&& proc_info) {
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
        Debug::print("system(", real_command_line, ")\n");
        fflush(nullptr);

        return system(real_command_line.c_str());
#endif
    }

    ExpectedL<int> cmd_execute(const Command& cmd_line, const WorkingDirectory& wd, const Environment& env)
    {
        const ElapsedTimer timer;
        auto maybe_result = cmd_execute_impl(cmd_line, wd, env);
        const auto elapsed = timer.us_64();
        g_subprocess_stats += elapsed;
        if (auto result = maybe_result.get())
        {
            Debug::print(fmt::format("cmd_execute() returned {} after {} us\n", *result, elapsed));
        }
        else
        {
            Debug::print(fmt::format("cmd_execute() returned ({}) after {} us\n", maybe_result.error(), elapsed));
        }

        return maybe_result;
    }

    ExpectedL<int> cmd_execute_and_stream_lines(const Command& cmd_line,
                                                std::function<void(StringView)> per_line_cb,
                                                const WorkingDirectory& wd,
                                                const Environment& env,
                                                Encoding encoding)
    {
        Strings::LinesStream lines;

        auto rc = cmd_execute_and_stream_data(
            cmd_line, [&](const StringView sv) { lines.on_data(sv, per_line_cb); }, wd, env, encoding);
        lines.on_end(per_line_cb);
        return rc;
    }

    ExpectedL<int> cmd_execute_and_stream_data(const Command& cmd_line,
                                               std::function<void(StringView)> data_cb,
                                               const WorkingDirectory& wd,
                                               const Environment& env,
                                               Encoding encoding)
    {
        const ElapsedTimer timer;
        static std::atomic_int32_t id_counter{1000};
        const auto id = fmt::format("{:4}", id_counter.fetch_add(1, std::memory_order_relaxed));
#if defined(_WIN32)
        using vcpkg::g_ctrl_c_state;

        g_ctrl_c_state.transition_to_spawn_process();
        ExpectedL<int> exit_code =
            windows_create_process_redirect(cmd_line.command_line(), wd, env, 0).map([&](ProcessInfoAndPipes&& output) {
                return output.wait_and_stream_output(data_cb, encoding);
            });
        g_ctrl_c_state.transition_from_spawn_process();
#else // ^^^ _WIN32 // !_WIN32 vvv
        Checks::check_exit(VCPKG_LINE_INFO, encoding == Encoding::Utf8);

        std::string actual_cmd_line;
        if (wd.working_directory.empty())
        {
            actual_cmd_line = fmt::format(R"({} {} 2>&1)", env.get(), cmd_line.command_line());
        }
        else
        {
            actual_cmd_line = Command("cd")
                                  .string_arg(wd.working_directory)
                                  .raw_arg("&&")
                                  .raw_arg(env.get())
                                  .raw_arg(cmd_line.command_line())
                                  .raw_arg("2>&1")
                                  .extract();
        }

        Debug::print(id, ": popen(", actual_cmd_line, ")\n");
        // Flush stdout before launching external process
        fflush(stdout);

        FILE* pipe = nullptr;
#if defined(__APPLE__)
        static std::mutex mtx;
#endif

        // Scope for lock guard
        {
#if defined(__APPLE__)
            // `popen` sometimes returns 127 on OSX when executed in parallel.
            // Related: https://github.com/microsoft/vcpkg-tool/pull/695#discussion_r973364608

            std::lock_guard guard(mtx);
#endif

            pipe = popen(actual_cmd_line.c_str(), "r");
        }

        if (pipe == nullptr)
        {
            return format_system_error_message("popen", errno);
        }

        char buf[1024];
        // Use fgets because fread will block until the entire buffer is filled.
        while (fgets(buf, 1024, pipe))
        {
            data_cb(StringView{buf, strlen(buf)});
        }

        if (!feof(pipe))
        {
            return format_system_error_message("feof", errno);
        }

        int ec;
        // Scope for lock guard
        {
#if defined(__APPLE__)
            // See the comment above at the call to `popen`.
            std::lock_guard guard(mtx);
#endif
            ec = pclose(pipe);
        }
        if (WIFEXITED(ec))
        {
            ec = WEXITSTATUS(ec);
        }
        else if (WIFSIGNALED(ec))
        {
            ec = WTERMSIG(ec);
        }
        else if (WIFSTOPPED(ec))
        {
            ec = WSTOPSIG(ec);
        }

        ExpectedL<int> exit_code = ec;
#endif /// ^^^ !_WIN32

        const auto elapsed = timer.us_64();
        g_subprocess_stats += elapsed;
        if (const auto pec = exit_code.get())
        {
            Debug::print(fmt::format("{}: cmd_execute_and_stream_data() returned {} after {:8} us\n",
                                     id,
                                     *pec,
                                     static_cast<unsigned long long>(elapsed)));
        }

        return exit_code;
    }

    ExpectedL<ExitCodeAndOutput> cmd_execute_and_capture_output(const Command& cmd_line,
                                                                const WorkingDirectory& wd,
                                                                const Environment& env,
                                                                Encoding encoding,
                                                                EchoInDebug echo_in_debug)
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
                   encoding)
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

}
