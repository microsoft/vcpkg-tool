#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/view.h>
#include <vcpkg/base/zstringview.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcpkg
{
    struct CMakeVariable
    {
        CMakeVariable(const StringView varname, const char* varvalue);
        CMakeVariable(const StringView varname, const std::string& varvalue);
        CMakeVariable(const StringView varname, const Path& varvalue);
        CMakeVariable(std::string var);

        std::string s;
    };

    void append_shell_escaped(std::string& target, StringView content);

    struct Command
    {
        Command() = default;
        explicit Command(StringView s) { string_arg(s); }

        Command& string_arg(StringView s) &;
        Command& raw_arg(StringView s) &
        {
            if (!buf.empty())
            {
                buf.push_back(' ');
            }

            buf.append(s.data(), s.size());
            return *this;
        }

        Command& forwarded_args(View<std::string> args) &
        {
            for (auto&& arg : args)
            {
                string_arg(arg);
            }

            return *this;
        }

        Command&& string_arg(StringView s) && { return std::move(string_arg(s)); };
        Command&& raw_arg(StringView s) && { return std::move(raw_arg(s)); }
        Command&& forwarded_args(View<std::string> args) && { return std::move(forwarded_args(args)); }

        std::string&& extract() && { return std::move(buf); }
        StringView command_line() const { return buf; }

        void clear() { buf.clear(); }
        bool empty() const { return buf.empty(); }

    private:
        std::string buf;
    };

    struct CommandLess
    {
        bool operator()(const Command& lhs, const Command& rhs) const
        {
            return lhs.command_line() < rhs.command_line();
        }
    };

    Command make_basic_cmake_cmd(const Path& cmake_tool_path,
                                 const Path& cmake_script,
                                 const std::vector<CMakeVariable>& pass_variables);

    Path get_exe_path_of_current_process();

    struct ExitCodeAndOutput
    {
        int exit_code;
        std::string output;
    };

    struct Environment
    {
#if defined(_WIN32)
        std::wstring m_env_data;
#else  // ^^^ _WIN32 // !_WIN32 vvv
        std::string m_env_data;
#endif // ^^^ !_WIN32
    };

    const Environment& get_clean_environment();
    Environment get_modified_clean_environment(const std::unordered_map<std::string, std::string>& extra_env,
                                               StringView prepend_to_path = {});

    struct WorkingDirectory
    {
        Path working_directory;
    };

    extern const WorkingDirectory default_working_directory;
    extern const Environment default_environment;

    [[nodiscard]] ExpectedS<int> cmd_execute(const Command& cmd_line,
                                             const WorkingDirectory& wd = default_working_directory,
                                             const Environment& env = default_environment);
    [[nodiscard]] ExpectedS<int> cmd_execute_clean(const Command& cmd_line,
                                                   const WorkingDirectory& wd = default_working_directory);

#if defined(_WIN32)
    Environment cmd_execute_and_capture_environment(const Command& cmd_line,
                                                    const Environment& env = default_environment);

    void cmd_execute_background(const Command& cmd_line);
#endif

    ExpectedS<ExitCodeAndOutput> cmd_execute_and_capture_output(const Command& cmd_line,
                                                                const WorkingDirectory& wd = default_working_directory,
                                                                const Environment& env = default_environment,
                                                                Encoding encoding = Encoding::Utf8,
                                                                EchoInDebug echo_in_debug = EchoInDebug::Hide);

    std::vector<ExpectedS<ExitCodeAndOutput>> cmd_execute_and_capture_output_parallel(
        View<Command> cmd_lines,
        const WorkingDirectory& wd = default_working_directory,
        const Environment& env = default_environment);

    [[nodiscard]] ExpectedS<int> cmd_execute_and_stream_lines(const Command& cmd_line,
                                                              std::function<void(StringView)> per_line_cb,
                                                              const WorkingDirectory& wd = default_working_directory,
                                                              const Environment& env = default_environment,
                                                              Encoding encoding = Encoding::Utf8);

    [[nodiscard]] ExpectedS<int> cmd_execute_and_stream_data(const Command& cmd_line,
                                                             std::function<void(StringView)> data_cb,
                                                             const WorkingDirectory& wd = default_working_directory,
                                                             const Environment& env = default_environment,
                                                             Encoding encoding = Encoding::Utf8);

    uint64_t get_subproccess_stats();

    void register_console_ctrl_handler();
#if defined(_WIN32)
    void initialize_global_job_object();
    void enter_interactive_subprocess();
    void exit_interactive_subprocess();
#endif
}
