#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <ctime>
#include <future>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#endif

#if defined(_WIN32)
#pragma comment(lib, "Advapi32")
#endif

namespace vcpkg
{
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
                            print2("Waiting for child processes to exit...\n");
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
                        print2("Waiting for child processes to exit...\n");
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
        auto rcode = sysctl(mib, 4, exePath, &len, NULL, 0);
        Checks::check_exit(VCPKG_LINE_INFO, rcode == 0, "Could not determine current executable path.");
        Checks::check_exit(VCPKG_LINE_INFO, len > 0, "Could not determine current executable path.");
        return Path(exePath, exePath + len - 1);
#elif defined(__OpenBSD__)
        const char* progname = getprogname();
        char resolved_path[PATH_MAX];
        auto ret = realpath(progname, resolved_path);
        Checks::check_exit(VCPKG_LINE_INFO, ret != nullptr, "Could not determine current executable path.");
        return resolved_path;
#else /* LINUX */
        std::array<char, 1024 * 4> buf;
        auto written = readlink("/proc/self/exe", buf.data(), buf.size());
        Checks::check_exit(VCPKG_LINE_INFO, written != -1, "Could not determine current executable path.");
        return Path(buf.data(), buf.data() + written);
#endif
    }

    CMakeVariable::CMakeVariable(const StringView varname, const char* varvalue)
        : s(Strings::format("-D%s=%s", varname, varvalue))
    {
    }
    CMakeVariable::CMakeVariable(const StringView varname, const std::string& varvalue)
        : CMakeVariable(varname, varvalue.c_str())
    {
    }
    CMakeVariable::CMakeVariable(const StringView varname, const Path& varvalue)
        : CMakeVariable(varname, varvalue.generic_u8string())
    {
    }
    CMakeVariable::CMakeVariable(std::string var) : s(std::move(var)) { }

    Command make_basic_cmake_cmd(const Path& cmake_tool_path,
                                 const Path& cmake_script,
                                 const std::vector<CMakeVariable>& pass_variables)
    {
        Command cmd{cmake_tool_path};
        for (auto&& var : pass_variables)
        {
            cmd.string_arg(var.s);
        }
        cmd.string_arg("-P").path_arg(cmake_script);
        return cmd;
    }

    Command& Command::string_arg(StringView s) &
    {
        if (!buf.empty()) buf.push_back(' ');
        if (Strings::find_first_of(s, " \t\n\r\"\\,;&`^|'") != s.end())
        {
            // TODO: improve this to properly handle all escaping
#if _WIN32
            // On Windows, `\`s before a double-quote must be doubled. Inner double-quotes must be escaped.
            buf.push_back('"');
            size_t n_slashes = 0;
            for (auto ch : s)
            {
                if (ch == '\\')
                {
                    ++n_slashes;
                }
                else if (ch == '"')
                {
                    buf.append(n_slashes + 1, '\\');
                    n_slashes = 0;
                }
                else
                {
                    n_slashes = 0;
                }
                buf.push_back(ch);
            }
            buf.append(n_slashes, '\\');
            buf.push_back('"');
#else
            // On non-Windows, `\` is the escape character and always requires doubling. Inner double-quotes must be
            // escaped.
            buf.push_back('"');
            for (auto ch : s)
            {
                if (ch == '\\' || ch == '"') buf.push_back('\\');
                buf.push_back(ch);
            }
            buf.push_back('"');
#endif
        }
        else
        {
            Strings::append(buf, s);
        }
        return *this;
    }

#if defined(_WIN32)
    Environment get_modified_clean_environment(const std::unordered_map<std::string, std::string>& extra_env,
                                               StringView prepend_to_path)
    {
        auto prepend_path_component = prepend_to_path.to_string();
        if (prepend_path_component.empty())
        {
            prepend_path_component.push_back(';');
        }

        static const std::string system_root_env =
            get_environment_variable("SystemRoot").value_or_exit(VCPKG_LINE_INFO);
        static const std::string system32_env = system_root_env + R"(\system32)";
        std::string new_path = Strings::format(R"(Path=%s%s;%s;%s\Wbem;%s\WindowsPowerShell\v1.0\)",
                                               prepend_path_component,
                                               system32_env,
                                               system_root_env,
                                               system32_env,
                                               system32_env);

        std::vector<std::wstring> env_wstrings = {
            L"ALLUSERSPROFILE",
            L"APPDATA",
            L"CommonProgramFiles",
            L"CommonProgramFiles(x86)",
            L"CommonProgramW6432",
            L"COMPUTERNAME",
            L"ComSpec",
            L"HOMEDRIVE",
            L"HOMEPATH",
            L"LOCALAPPDATA",
            L"LOGONSERVER",
            L"NUMBER_OF_PROCESSORS",
            L"OS",
            L"PATHEXT",
            L"PROCESSOR_ARCHITECTURE",
            L"PROCESSOR_ARCHITEW6432",
            L"PROCESSOR_IDENTIFIER",
            L"PROCESSOR_LEVEL",
            L"PROCESSOR_REVISION",
            L"ProgramData",
            L"ProgramFiles",
            L"ProgramFiles(x86)",
            L"ProgramW6432",
            L"PROMPT",
            L"PSModulePath",
            L"PUBLIC",
            L"SystemDrive",
            L"SystemRoot",
            L"TEMP",
            L"TMP",
            L"USERDNSDOMAIN",
            L"USERDOMAIN",
            L"USERDOMAIN_ROAMINGPROFILE",
            L"USERNAME",
            L"USERPROFILE",
            L"windir",
            // Enables proxy information to be passed to Curl, the underlying download library in cmake.exe
            L"http_proxy",
            L"https_proxy",
            // Environment variables to tell git to use custom SSH executable or command
            L"GIT_SSH",
            L"GIT_SSH_COMMAND",
            // Environment variables needed for ssh-agent based authentication
            L"SSH_AUTH_SOCK",
            L"SSH_AGENT_PID",
            // Enables find_package(CUDA) and enable_language(CUDA) in CMake
            L"CUDA_PATH",
            L"CUDA_PATH_V9_0",
            L"CUDA_PATH_V9_1",
            L"CUDA_PATH_V10_0",
            L"CUDA_PATH_V10_1",
            L"CUDA_PATH_V10_2",
            L"CUDA_PATH_V11_0",
            L"CUDA_PATH_V11_1",
            L"CUDA_PATH_V11_2",
            L"CUDA_TOOLKIT_ROOT_DIR",
            // Environment variable generated automatically by CUDA after installation
            L"NVCUDASAMPLES_ROOT",
            L"NVTOOLSEXT_PATH",
            // Enables find_package(Vulkan) in CMake. Environment variable generated by Vulkan SDK installer
            L"VULKAN_SDK",
            // Enable targeted Android NDK
            L"ANDROID_NDK_HOME",
            // Environment variables generated automatically by Intel oneAPI after installation
            L"ONEAPI_ROOT",
            L"IFORT_COMPILER19",
            L"IFORT_COMPILER20",
            L"IFORT_COMPILER21",
            // Environment variables used by wrapper scripts to allow us to set environment variables in parent shells
            L"Z_VCPKG_POSTSCRIPT",
            L"Z_VCPKG_UNDO",
        };

        const Optional<std::string> keep_vars = get_environment_variable("VCPKG_KEEP_ENV_VARS");
        const auto k = keep_vars.get();

        if (k && !k->empty())
        {
            auto vars = Strings::split(*k, ';');

            for (auto&& var : vars)
            {
                env_wstrings.push_back(Strings::to_utf16(var));
            }
        }

        std::wstring env_cstr;

        for (auto&& env_wstring : env_wstrings)
        {
            const Optional<std::string> value = get_environment_variable(Strings::to_utf8(env_wstring.c_str()));
            const auto v = value.get();
            if (!v || v->empty()) continue;

            env_cstr.append(env_wstring);
            env_cstr.push_back(L'=');
            env_cstr.append(Strings::to_utf16(*v));
            env_cstr.push_back(L'\0');
        }

        if (extra_env.find("PATH") != extra_env.end())
            new_path += Strings::format(";%s", extra_env.find("PATH")->second);
        env_cstr.append(Strings::to_utf16(new_path));
        env_cstr.push_back(L'\0');
        env_cstr.append(L"VSLANG=1033");
        env_cstr.push_back(L'\0');
        env_cstr.append(L"VSCMD_SKIP_SENDTELEMETRY=1");
        env_cstr.push_back(L'\0');

        for (const auto& item : extra_env)
        {
            if (item.first == "PATH") continue;
            env_cstr.append(Strings::to_utf16(item.first));
            env_cstr.push_back(L'=');
            env_cstr.append(Strings::to_utf16(item.second));
            env_cstr.push_back(L'\0');
        }

        return {env_cstr};
    }
#else
    Environment get_modified_clean_environment(const std::unordered_map<std::string, std::string>&, StringView)
    {
        return {};
    }
#endif
    const Environment& get_clean_environment()
    {
        static const Environment clean_env = get_modified_clean_environment({});
        return clean_env;
    }

    std::vector<ExitCodeAndOutput> cmd_execute_and_capture_output_parallel(View<Command> cmd_lines,
                                                                           InWorkingDirectory wd,
                                                                           const Environment& env)
    {
        if (cmd_lines.size() == 0)
        {
            return {};
        }
        if (cmd_lines.size() == 1)
        {
            return {cmd_execute_and_capture_output(cmd_lines[0], wd, env)};
        }
        std::vector<ExitCodeAndOutput> res(cmd_lines.size());
        std::atomic<size_t> work_item{0};

        const auto num_threads =
            static_cast<size_t>(std::max(1, std::min(get_concurrency(), static_cast<int>(cmd_lines.size()))));

        auto work = [&cmd_lines, &res, &work_item, &wd, &env]() {
            for (size_t item = work_item.fetch_add(1); item < cmd_lines.size(); item = work_item.fetch_add(1))
            {
                res[item] = cmd_execute_and_capture_output(cmd_lines[item], wd, env);
            }
        };

        std::vector<std::future<void>> workers;
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

    int cmd_execute_clean(const Command& cmd_line, InWorkingDirectory wd)
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
    static ExpectedT<ProcessInfo, unsigned long> windows_create_process(StringView cmd_line,
                                                                        InWorkingDirectory wd,
                                                                        const Environment& env,
                                                                        DWORD dwCreationFlags,
                                                                        STARTUPINFOW& startup_info) noexcept
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

        VCPKG_MSVC_WARNING(suppress : 6335) // Leaking process information handle 'process_info.proc_info.hProcess'
                                            // /analyze can't tell that we transferred ownership here
        bool succeeded =
            TRUE == CreateProcessW(nullptr,
                                   Strings::to_utf16(cmd_line).data(),
                                   nullptr,
                                   nullptr,
                                   TRUE,
                                   IDLE_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT | dwCreationFlags,
                                   env.m_env_data.empty()
                                       ? nullptr
                                       : const_cast<void*>(static_cast<const void*>(env.m_env_data.data())),
                                   working_directory.empty() ? nullptr : working_directory.data(),
                                   &startup_info,
                                   &process_info.proc_info);

        if (succeeded)
            return process_info;
        else
            return GetLastError();
    }

    static ExpectedT<ProcessInfo, unsigned long> windows_create_windowless_process(StringView cmd_line,
                                                                                   InWorkingDirectory wd,
                                                                                   const Environment& env,
                                                                                   DWORD dwCreationFlags) noexcept
    {
        STARTUPINFOW startup_info;
        memset(&startup_info, 0, sizeof(STARTUPINFOW));
        startup_info.cb = sizeof(STARTUPINFOW);
        startup_info.dwFlags = STARTF_USESHOWWINDOW;
        startup_info.wShowWindow = SW_HIDE;

        return windows_create_process(cmd_line, wd, env, dwCreationFlags, startup_info);
    }

    struct ProcessInfoAndPipes
    {
        ProcessInfo proc_info;
        HANDLE child_stdin = 0;
        HANDLE child_stdout = 0;

        template<class Function>
        int wait_and_stream_output(Function&& f)
        {
            CloseHandle(child_stdin);

            unsigned long bytes_read = 0;
            static constexpr int buffer_size = 1024 * 32;
            auto buf = std::make_unique<char[]>(buffer_size);
            while (ReadFile(child_stdout, (void*)buf.get(), buffer_size, &bytes_read, nullptr) && bytes_read > 0)
            {
                f(StringView{buf.get(), static_cast<size_t>(bytes_read)});
            }

            CloseHandle(child_stdout);

            return proc_info.wait();
        }
    };

    static ExpectedT<ProcessInfoAndPipes, unsigned long> windows_create_process_redirect(StringView cmd_line,
                                                                                         InWorkingDirectory wd,
                                                                                         const Environment& env,
                                                                                         DWORD dwCreationFlags) noexcept
    {
        ProcessInfoAndPipes ret;

        STARTUPINFOW startup_info;
        memset(&startup_info, 0, sizeof(STARTUPINFOW));
        startup_info.cb = sizeof(STARTUPINFOW);
        startup_info.dwFlags |= STARTF_USESTDHANDLES;

        SECURITY_ATTRIBUTES saAttr;
        memset(&saAttr, 0, sizeof(SECURITY_ATTRIBUTES));
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        // Create a pipe for the child process's STDOUT.
        if (!CreatePipe(&ret.child_stdout, &startup_info.hStdOutput, &saAttr, 0)) Checks::exit_fail(VCPKG_LINE_INFO);
        // Ensure the read handle to the pipe for STDOUT is not inherited.
        if (!SetHandleInformation(ret.child_stdout, HANDLE_FLAG_INHERIT, 0)) Checks::exit_fail(VCPKG_LINE_INFO);
        // Create a pipe for the child process's STDIN.
        if (!CreatePipe(&startup_info.hStdInput, &ret.child_stdin, &saAttr, 0)) Checks::exit_fail(VCPKG_LINE_INFO);
        // Ensure the write handle to the pipe for STDIN is not inherited.
        if (!SetHandleInformation(ret.child_stdin, HANDLE_FLAG_INHERIT, 0)) Checks::exit_fail(VCPKG_LINE_INFO);
        startup_info.hStdError = startup_info.hStdOutput;

        auto maybe_proc_info = windows_create_process(cmd_line, wd, env, dwCreationFlags, startup_info);

        CloseHandle(startup_info.hStdInput);
        CloseHandle(startup_info.hStdOutput);

        if (auto proc_info = maybe_proc_info.get())
        {
            ret.proc_info = std::move(*proc_info);
            return ret;
        }

        return maybe_proc_info.error();
    }
#endif

#if defined(_WIN32)
    void cmd_execute_background(const Command& cmd_line)
    {
        auto timer = ElapsedTimer::create_started();

        auto process_info =
            windows_create_windowless_process(cmd_line.command_line(),
                                              InWorkingDirectory{Path()},
                                              {},
                                              CREATE_NEW_CONSOLE | CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB);
        if (!process_info.get())
        {
            Debug::print("cmd_execute_background() failed with error code ", process_info.error(), "\n");
        }

        Debug::print("cmd_execute_background() took ", static_cast<int>(timer.microseconds()), " us\n");
    }

    Environment cmd_execute_and_capture_environment(const Command& cmd_line, const Environment& env)
    {
        static StringLiteral magic_string = "cdARN4xjKueKScMy9C6H";

        auto actual_cmd_line = cmd_line;
        actual_cmd_line.raw_arg(Strings::concat(" & echo ", magic_string, " & set"));

        auto rc_output = cmd_execute_and_capture_output(actual_cmd_line, env);
        Checks::check_exit(VCPKG_LINE_INFO,
                           rc_output.exit_code == 0,
                           "Run vcvarsall.bat to get Visual Studio env failed with exit code %d",
                           rc_output.exit_code);
        Debug::print("command line: ", actual_cmd_line.command_line(), "\n");
        Debug::print(rc_output.output, "\n");

        auto it = Strings::search(rc_output.output, magic_string);
        const char* const last = rc_output.output.data() + rc_output.output.size();

        Checks::check_exit(VCPKG_LINE_INFO, it != last);
        // find the first non-whitespace character after the magic string
        it = std::find_if_not(it + magic_string.size(), last, ::isspace);
        Checks::check_exit(VCPKG_LINE_INFO, it != last);

        std::wstring out_env;

        for (;;)
        {
            auto equal_it = std::find(it, last, '=');
            if (equal_it == last) break;
            StringView variable_name(it, equal_it);
            auto newline_it = std::find(equal_it + 1, last, '\r');
            if (newline_it == last) break;
            StringView value(equal_it + 1, newline_it);

            out_env.append(Strings::to_utf16(Strings::concat(variable_name, '=', value)));
            out_env.push_back(L'\0');

            it = newline_it + 1;
            if (it != last && *it == '\n') ++it;
        }

        return {std::move(out_env)};
    }
#endif

    int cmd_execute(const Command& cmd_line, InWorkingDirectory wd, const Environment& env)
    {
        auto timer = ElapsedTimer::create_started();
#if defined(_WIN32)
        using vcpkg::g_ctrl_c_state;
        g_ctrl_c_state.transition_to_spawn_process();
        auto proc_info = windows_create_windowless_process(cmd_line.command_line(), wd, env, 0);
        auto long_exit_code = [&]() -> unsigned long {
            if (auto p = proc_info.get())
                return p->wait();
            else
                return proc_info.error();
        }();
        if (long_exit_code > INT_MAX) long_exit_code = INT_MAX;
        int exit_code = static_cast<int>(long_exit_code);
        g_ctrl_c_state.transition_from_spawn_process();
#else
        (void)env;
        std::string real_command_line;
        if (wd.working_directory.empty())
        {
            real_command_line = cmd_line.command_line().to_string();
        }
        else
        {
            real_command_line =
                Command("cd").path_arg(wd.working_directory).raw_arg("&&").raw_arg(cmd_line.command_line()).extract();
        }
        Debug::print("system(", real_command_line, ")\n");
        fflush(nullptr);

        int exit_code = system(real_command_line.c_str());
#endif
        const auto elapsed = timer.us_64();
        g_subprocess_stats += elapsed;
        Debug::print("cmd_execute() returned ", exit_code, " after ", elapsed, " us\n");
        return exit_code;
    }

    int cmd_execute_and_stream_lines(const Command& cmd_line,
                                     InWorkingDirectory wd,
                                     std::function<void(StringView)> per_line_cb,
                                     const Environment& env)
    {
        Strings::LinesStream lines;

        auto rc = cmd_execute_and_stream_data(
            cmd_line, wd, [&](const StringView sv) { lines.on_data(sv, per_line_cb); }, env);

        lines.on_end(per_line_cb);
        return rc;
    }

    int cmd_execute_and_stream_data(const Command& cmd_line,
                                    InWorkingDirectory wd,
                                    std::function<void(StringView)> data_cb,
                                    const Environment& env)
    {
        const auto timer = ElapsedTimer::create_started();

#if defined(_WIN32)
        using vcpkg::g_ctrl_c_state;

        g_ctrl_c_state.transition_to_spawn_process();
        auto maybe_proc_info = windows_create_process_redirect(cmd_line.command_line(), wd, env, 0);
        auto exit_code = [&]() -> unsigned long {
            if (auto p = maybe_proc_info.get())
                return p->wait_and_stream_output(data_cb);
            else
                return maybe_proc_info.error();
        }();
        g_ctrl_c_state.transition_from_spawn_process();
#else
        (void)env;
        std::string actual_cmd_line;
        if (wd.working_directory.empty())
        {
            actual_cmd_line = Strings::format(R"(%s 2>&1)", cmd_line.command_line());
        }
        else
        {
            actual_cmd_line = Command("cd")
                                  .path_arg(wd.working_directory)
                                  .raw_arg("&&")
                                  .raw_arg(cmd_line.command_line())
                                  .raw_arg("2>&1")
                                  .extract();
        }

        Debug::print("popen(", actual_cmd_line, ")\n");
        // Flush stdout before launching external process
        fflush(stdout);

        const auto pipe = popen(actual_cmd_line.c_str(), "r");
        if (pipe == nullptr)
        {
            return 1;
        }
        char buf[1024];
        while (fgets(buf, 1024, pipe))
        {
            data_cb(StringView{buf, strlen(buf)});
        }

        if (!feof(pipe))
        {
            return 1;
        }

        const auto exit_code = pclose(pipe);
#endif
        const auto elapsed = timer.us_64();
        g_subprocess_stats += elapsed;
        Debug::print("cmd_execute_and_stream_data() returned ",
                     exit_code,
                     " after ",
                     Strings::format("%8llu", static_cast<unsigned long long>(elapsed)),
                     " us\n");

        return exit_code;
    }

    ExitCodeAndOutput cmd_execute_and_capture_output(const Command& cmd_line,
                                                     InWorkingDirectory wd,
                                                     const Environment& env)
    {
        std::string output;
        auto rc = cmd_execute_and_stream_data(
            cmd_line, wd, [&](StringView sv) { Strings::append(output, sv); }, env);
        return {rc, std::move(output)};
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
}
