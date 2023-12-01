#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace vcpkg
{
    struct CMakeVariable
    {
        CMakeVariable(const StringView varname, const char* varvalue);
        CMakeVariable(const StringView varname, const std::string& varvalue);
        CMakeVariable(const StringView varname, StringLiteral varvalue);
        CMakeVariable(const StringView varname, const Path& varvalue);
        CMakeVariable(const std::string& var);

        std::string s;
    };

    std::string format_cmake_variable(StringView key, StringView value);
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

    struct ProcessLaunchSettings
    {
        Command cmd; // must be the first member for several callers who use aggregate initialization
        Optional<Path> working_directory;
        Optional<Environment> environment;

        ProcessLaunchSettings& string_arg(StringView s) &
        {
            cmd.string_arg(s);
            return *this;
        }

        ProcessLaunchSettings& raw_arg(StringView s) &
        {
            cmd.raw_arg(s);
            return *this;
        }

        ProcessLaunchSettings& forwarded_args(View<std::string> args) &
        {
            cmd.forwarded_args(args);
            return *this;
        }

        ProcessLaunchSettings&& string_arg(StringView s) && { return std::move(string_arg(s)); };
        ProcessLaunchSettings&& raw_arg(StringView s) && { return std::move(raw_arg(s)); }
        ProcessLaunchSettings&& forwarded_args(View<std::string> args) && { return std::move(forwarded_args(args)); }

        StringView command_line() const { return cmd.command_line(); }
    };

    struct RedirectedProcessLaunchSettings : ProcessLaunchSettings
    {
#if defined(_WIN32)
        // the encoding to use for standard streams of the child
        Encoding encoding = Encoding::Utf8;
#endif // ^^^ _WIN32
       // whether to echo all read content to the enclosing terminal;
       // only affects cmd_execute-family commands that redirect the output
        EchoInDebug echo_in_debug = EchoInDebug::Hide;
        std::string stdin_content;

        RedirectedProcessLaunchSettings& string_arg(StringView s) &
        {
            cmd.string_arg(s);
            return *this;
        }

        RedirectedProcessLaunchSettings& raw_arg(StringView s) &
        {
            cmd.raw_arg(s);
            return *this;
        }

        RedirectedProcessLaunchSettings& forwarded_args(View<std::string> args) &
        {
            cmd.forwarded_args(args);
            return *this;
        }

        RedirectedProcessLaunchSettings&& string_arg(StringView s) && { return std::move(string_arg(s)); };
        RedirectedProcessLaunchSettings&& raw_arg(StringView s) && { return std::move(raw_arg(s)); }
        RedirectedProcessLaunchSettings&& forwarded_args(View<std::string> args) &&
        {
            return std::move(forwarded_args(args));
        }
    };

    ExpectedL<int> cmd_execute(const ProcessLaunchSettings& settings);

#if defined(_WIN32)
    Environment cmd_execute_and_capture_environment(const Command& cmd_line, const Environment& env);
#endif

    void cmd_execute_background(const Command& cmd_line);

    ExpectedL<ExitCodeAndOutput> cmd_execute_and_capture_output(const RedirectedProcessLaunchSettings& settings);

    std::vector<ExpectedL<ExitCodeAndOutput>> cmd_execute_and_capture_output_parallel(
        View<RedirectedProcessLaunchSettings> settings);

    ExpectedL<int> cmd_execute_and_stream_lines(const RedirectedProcessLaunchSettings& settings,
                                                const std::function<void(StringView)>& per_line_cb);

    ExpectedL<int> cmd_execute_and_stream_data(const RedirectedProcessLaunchSettings& settings,
                                               const std::function<void(StringView)>& data_cb);

    uint64_t get_subproccess_stats();

    void register_console_ctrl_handler();
#if defined(_WIN32)
    void initialize_global_job_object();
    void enter_interactive_subprocess();
    void exit_interactive_subprocess();
#endif

    struct ProcessStat
    {
        int ppid;
        std::string executable_name;
    };

    Optional<ProcessStat> try_parse_process_stat_file(const FileContents& contents);
    void get_parent_process_list(std::vector<std::string>& ret);

    bool succeeded(const ExpectedL<int>& maybe_exit) noexcept;

    // If exit code is 0, returns a 'success' ExpectedL.
    // Otherwise, returns an ExpectedL containing error text
    ExpectedL<Unit> flatten(const ExpectedL<ExitCodeAndOutput>&, StringView tool_name);

    // If exit code is 0, returns a 'success' ExpectedL containing the output
    // Otherwise, returns an ExpectedL containing error text
    ExpectedL<std::string> flatten_out(ExpectedL<ExitCodeAndOutput>&&, StringView tool_name);
}
