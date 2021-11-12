#pragma once

#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/base/files.h>
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

    struct Command
    {
        Command() = default;
        explicit Command(const Path& p) { path_arg(p); }
        explicit Command(StringView s) { string_arg(s); }
        explicit Command(const std::string& s) { string_arg(s); }
        explicit Command(const char* s) { string_arg({s, ::strlen(s)}); }

        Command& path_arg(const Path& p) & { return string_arg(p); }
        Command& string_arg(StringView s) &;
        Command& raw_arg(StringView s) &
        {
            buf.push_back(' ');
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

        Command&& path_arg(const Path& p) && { return std::move(path_arg(p)); }
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
#endif
    };

    const Environment& get_clean_environment();
    Environment get_modified_clean_environment(const std::unordered_map<std::string, std::string>& extra_env,
                                               StringView prepend_to_path = {});

    struct InWorkingDirectory
    {
        const Path& working_directory;
    };

    int cmd_execute(const Command& cmd_line, InWorkingDirectory wd, const Environment& env = {});
    inline int cmd_execute(const Command& cmd_line, const Environment& env = {})
    {
        return cmd_execute(cmd_line, InWorkingDirectory{Path()}, env);
    }

    int cmd_execute_clean(const Command& cmd_line, InWorkingDirectory wd);
    inline int cmd_execute_clean(const Command& cmd_line)
    {
        return cmd_execute_clean(cmd_line, InWorkingDirectory{Path()});
    }

#if defined(_WIN32)
    Environment cmd_execute_and_capture_environment(const Command& cmd_line, const Environment& env = {});

    void cmd_execute_background(const Command& cmd_line);
#endif

    ExitCodeAndOutput cmd_execute_and_capture_output(const Command& cmd_line,
                                                     InWorkingDirectory wd,
                                                     const Environment& env = {});
    inline ExitCodeAndOutput cmd_execute_and_capture_output(const Command& cmd_line, const Environment& env = {})
    {
        return cmd_execute_and_capture_output(cmd_line, InWorkingDirectory{Path()}, env);
    }

    std::vector<ExitCodeAndOutput> cmd_execute_and_capture_output_parallel(View<Command> cmd_lines,
                                                                           InWorkingDirectory wd,
                                                                           const Environment& env = {});

    inline std::vector<ExitCodeAndOutput> cmd_execute_and_capture_output_parallel(View<Command> cmd_lines,
                                                                                  const Environment& env = {})
    {
        return cmd_execute_and_capture_output_parallel(cmd_lines, InWorkingDirectory{Path()}, env);
    }

    int cmd_execute_and_stream_lines(const Command& cmd_line,
                                     InWorkingDirectory wd,
                                     std::function<void(StringView)> per_line_cb,
                                     const Environment& env = {});
    inline int cmd_execute_and_stream_lines(const Command& cmd_line,
                                            std::function<void(StringView)> per_line_cb,
                                            const Environment& env = {})
    {
        return cmd_execute_and_stream_lines(cmd_line, InWorkingDirectory{Path()}, std::move(per_line_cb), env);
    }

    int cmd_execute_and_stream_data(const Command& cmd_line,
                                    InWorkingDirectory wd,
                                    std::function<void(StringView)> data_cb,
                                    const Environment& env = {});
    inline int cmd_execute_and_stream_data(const Command& cmd_line,
                                           std::function<void(StringView)> data_cb,
                                           const Environment& env = {})
    {
        return cmd_execute_and_stream_data(cmd_line, InWorkingDirectory{Path()}, std::move(data_cb), env);
    }

    uint64_t get_subproccess_stats();

    void register_console_ctrl_handler();
#if defined(_WIN32)
    void initialize_global_job_object();
    void enter_interactive_subprocess();
    void exit_interactive_subprocess();
#endif
}
