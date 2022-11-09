#pragma once

#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/view.h>

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
        const char* c_str() const { return buf.c_str(); }

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
        using string_t = std::wstring;
#else
        using string_t = std::string;
#endif
        void add_entry(StringView key, StringView value);
        const string_t& get() const;

    private:
        string_t m_env_data;
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

    ExpectedL<int> cmd_execute(const Command& cmd_line,
                               const WorkingDirectory& wd = default_working_directory,
                               const Environment& env = default_environment);
    ExpectedL<int> cmd_execute_clean(const Command& cmd_line, const WorkingDirectory& wd = default_working_directory);

#if defined(_WIN32)
    Environment cmd_execute_and_capture_environment(const Command& cmd_line,
                                                    const Environment& env = default_environment);
#endif

    void cmd_execute_background(const Command& cmd_line);

    ExpectedL<ExitCodeAndOutput> cmd_execute_and_capture_output(const Command& cmd_line,
                                                                const WorkingDirectory& wd = default_working_directory,
                                                                const Environment& env = default_environment,
                                                                Encoding encoding = Encoding::Utf8,
                                                                EchoInDebug echo_in_debug = EchoInDebug::Hide);

    std::vector<ExpectedL<ExitCodeAndOutput>> cmd_execute_and_capture_output_parallel(
        View<Command> cmd_lines,
        const WorkingDirectory& wd = default_working_directory,
        const Environment& env = default_environment);

    ExpectedL<int> cmd_execute_and_stream_lines(const Command& cmd_line,
                                                std::function<void(StringView)> per_line_cb,
                                                const WorkingDirectory& wd = default_working_directory,
                                                const Environment& env = default_environment,
                                                Encoding encoding = Encoding::Utf8);

    ExpectedL<int> cmd_execute_and_stream_data(const Command& cmd_line,
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

    bool succeeded(const ExpectedL<int>& maybe_exit) noexcept;

    // If exit code is 0, returns a 'success' ExpectedL.
    // Otherwise, returns an ExpectedL containing error text
    ExpectedL<Unit> flatten(const ExpectedL<ExitCodeAndOutput>&, StringView tool_name);

    // If exit code is 0, returns a 'success' ExpectedL containing the output
    // Otherwise, returns an ExpectedL containing error text
    ExpectedL<std::string> flatten_out(ExpectedL<ExitCodeAndOutput>&&, StringView tool_name);
}
