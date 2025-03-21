#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/fmt.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/tools.h>

namespace
{
    using namespace vcpkg;

    Command git_cmd_builder(const GitConfig& config)
    {
        auto cmd = Command(config.git_exe);
        cmd.string_arg("-c").string_arg("core.autocrlf=false");
        if (!config.git_dir.empty())
        {
            cmd.string_arg(Strings::concat("--git-dir=", config.git_dir));
        }
        if (!config.git_work_tree.empty())
        {
            cmd.string_arg(Strings::concat("--work-tree=", config.git_work_tree));
        }
        return cmd;
    }
}

namespace vcpkg
{
    ExpectedL<std::vector<GitStatusLine>> parse_git_status_output(StringView output, StringView cmd_line)
    {
        // Output of git status --porcelain=v1 is in the form:
        //
        // XY ORIG_PATH
        // or
        // XY ORIG_PATH -> NEW_PATH
        //
        // X: is the status on the index
        // Y: is the status on the work tree
        // ORIG_PATH: is the original filepath for rename operations
        // PATH: is the path of the modified file
        //
        // https://git-scm.com/docs/git-status
        auto extract_status = [](ParserBase& parser, GitStatusLine::Status& into) -> bool {
            using Status = GitStatusLine::Status;

            auto c = parser.cur();
            switch (c)
            {
                case ' ': into = Status::Unmodified; break;
                case 'M': into = Status::Modified; break;
                case 'T': into = Status::TypeChanged; break;
                case 'A': into = Status::Added; break;
                case 'D': into = Status::Deleted; break;
                case 'R': into = Status::Renamed; break;
                case 'C': into = Status::Copied; break;
                case 'U': into = Status::Unmerged; break;
                case '?': into = Status::Untracked; break;
                case '!': into = Status::Ignored; break;
                default:
                    parser.add_error(msg::format(msgGitStatusUnknownFileStatus, msg::value = static_cast<char>(c)),
                                     parser.cur_loc());
                    into = Status::Unknown;
            }
            parser.next();
            return Status::Unknown != into;
        };

        std::vector<GitStatusLine> results;
        ParserBase parser(output, "git status", {0, 0});
        while (!parser.at_eof())
        {
            GitStatusLine result;

            // Parse "XY"
            if (!extract_status(parser, result.index_status) || !extract_status(parser, result.work_tree_status))
            {
                break;
            }
            parser.skip_tabs_spaces();

            // Parse "ORIG_PATH"
            auto orig_path = parser.match_until(ParserBase::is_whitespace).to_string();
            if (ParserBase::is_lineend(parser.cur()))
            {
                result.path = orig_path;
            }
            else
            {
                // Parse "-> NEW_PATH"
                parser.skip_tabs_spaces();
                if (parser.try_match_keyword("->"))
                {
                    parser.skip_tabs_spaces();
                    if (ParserBase::is_lineend(parser.cur()))
                    {
                        parser.add_error(msg::format(msgGitStatusOutputExpectedFileName), parser.cur_loc());
                        break;
                    }
                    auto path = parser.match_until(ParserBase::is_whitespace).to_string();
                    result.old_path = orig_path;
                    result.path = path;
                }
                else
                {
                    parser.add_error(msg::format(msgGitStatusOutputExpectedRenameOrNewline), parser.cur_loc());
                    break;
                }
            }

            if (!ParserBase::is_lineend(parser.cur()))
            {
                parser.add_error(msg::format(msgGitStatusOutputExpectedNewLine), parser.cur_loc());
                break;
            }

            parser.next();
            results.push_back(result);
        }

        if (auto error = parser.get_error())
        {
            return msg::format(msgGitUnexpectedCommandOutputCmd, msg::command_line = cmd_line)
                .append_raw('\n')
                .append_raw(error->to_string());
        }

        return results;
    }

    ExpectedL<std::vector<GitStatusLine>> git_status(const GitConfig& config, StringView path)
    {
        auto cmd = git_cmd_builder(config).string_arg("status").string_arg("--porcelain=v1");
        if (!path.empty())
        {
            cmd.string_arg("--").string_arg(path);
        }

        auto maybe_output = cmd_execute_and_capture_output(cmd);
        if (auto output = maybe_output.get())
        {
            if (output->exit_code != 0)
            {
                return msg::format(msgGitCommandFailed, msg::command_line = cmd.command_line())
                    .append_raw('\n')
                    .append_raw(output->output);
            }

            return parse_git_status_output(output->output, cmd.command_line());
        }

        return msg::format(msgGitCommandFailed, msg::command_line = cmd.command_line())
            .append_raw('\n')
            .append_raw(maybe_output.error().to_string());
    }

    ExpectedL<bool> is_shallow_clone(const GitConfig& config)
    {
        return flatten_out(cmd_execute_and_capture_output(
                               git_cmd_builder(config).string_arg("rev-parse").string_arg("--is-shallow-repository")),
                           Tools::GIT)
            .map([](std::string&& output) { return "true" == Strings::trim(std::move(output)); });
    }

    Optional<std::string> git_prefix(DiagnosticContext& context, const Path& git_exe, const Path& target)
    {
        Command cmd(git_exe);
        RedirectedProcessLaunchSettings launch_settings;
        launch_settings.working_directory.emplace(target);
        cmd.string_arg("rev-parse");
        cmd.string_arg("--show-prefix");

        auto maybe_prefix_result = cmd_execute_and_capture_output(context, cmd, launch_settings);
        if (auto prefix_output = check_zero_exit_code(context, maybe_prefix_result, git_exe))
        {
            Strings::inplace_trim_end(*prefix_output);
            return std::move(*prefix_output);
        }

        return nullopt;
    }

    Optional<std::string> git_index_file(DiagnosticContext& context, const Path& git_exe, const Path& target)
    {
        Command cmd(git_exe);
        cmd.string_arg("-c").string_arg("core.autocrlf=false");
        RedirectedProcessLaunchSettings launch_settings;
        launch_settings.working_directory.emplace(target);
        cmd.string_arg("rev-parse");
        cmd.string_arg("--path-format=absolute");
        cmd.string_arg("--git-path");
        cmd.string_arg("index");

        auto maybe_index_result = cmd_execute_and_capture_output(context, cmd, launch_settings);
        if (auto index_output = check_zero_exit_code(context, maybe_index_result, git_exe))
        {
            Strings::inplace_trim_end(*index_output);
            return std::move(*index_output);
        }

        return nullopt;
    }

    bool git_add_with_index(DiagnosticContext& context, const Path& git_exe, const Path& index_file, const Path& target)
    {
        RedirectedProcessLaunchSettings launch_settings;
        launch_settings.working_directory.emplace(target);
        auto& environment = launch_settings.environment.emplace();
        environment.add_entry("GIT_INDEX_FILE", index_file);
        Command cmd(git_exe);
        cmd.string_arg("-c").string_arg("core.autocrlf=false");
        cmd.string_arg("add");
        cmd.string_arg("-A");
        cmd.string_arg(".");
        auto maybe_add_result = cmd_execute_and_capture_output(context, cmd, launch_settings);
        if (check_zero_exit_code(context, maybe_add_result, git_exe))
        {
            return true;
        }

        return false;
    }

    Optional<std::string> git_write_index_tree(DiagnosticContext& context,
                                               const Path& git_exe,
                                               const Path& index_file,
                                               const Path& working_directory)
    {
        Command cmd(git_exe);
        cmd.string_arg("-c").string_arg("core.autocrlf=false");
        RedirectedProcessLaunchSettings launch_settings;
        launch_settings.working_directory.emplace(working_directory);
        auto& environment = launch_settings.environment.emplace();
        environment.add_entry("GIT_INDEX_FILE", index_file);
        cmd.string_arg("write-tree");
        auto maybe_write_tree_result = cmd_execute_and_capture_output(context, cmd, launch_settings);
        if (auto tree_output = check_zero_exit_code(context, maybe_write_tree_result, git_exe))
        {
            Strings::inplace_trim_end(*tree_output);
            return std::move(*tree_output);
        }

        return nullopt;
    }

    Optional<std::vector<GitLSTreeEntry>> ls_tree(DiagnosticContext& context,
                                                  const Path& git_exe,
                                                  const Path& working_directory,
                                                  StringView treeish)
    {
        Command cmd(git_exe);
        cmd.string_arg("-c").string_arg("core.autocrlf=false");
        RedirectedProcessLaunchSettings launch_settings;
        launch_settings.working_directory.emplace(working_directory);
        cmd.string_arg("ls-tree");
        cmd.string_arg(treeish);
        cmd.string_arg("--full-tree");
        auto maybe_ls_tree_result = cmd_execute_and_capture_output(context, cmd, launch_settings);
        if (auto ls_tree_output = check_zero_exit_code(context, maybe_ls_tree_result, git_exe))
        {
            std::vector<GitLSTreeEntry> ret;
            const auto lines = Strings::split(*ls_tree_output, '\n');
            // The first line of the output is always the parent directory itself.
            for (auto&& line : lines)
            {
                // The default output comes in the format:
                // <mode> SP <type> SP <object> TAB <file>
                auto split_line = Strings::split(line, '\t');
                if (split_line.size() != 2)
                {
                    context.report_error_with_log(
                        *ls_tree_output, msgGitUnexpectedCommandOutputCmd, msg::command_line = cmd.command_line());
                    return nullopt;
                }

                auto file_info_section = Strings::split(split_line[0], ' ');
                if (file_info_section.size() != 3)
                {
                    context.report_error_with_log(
                        *ls_tree_output, msgGitUnexpectedCommandOutputCmd, msg::command_line = cmd.command_line());
                    return nullopt;
                }

                ret.push_back(GitLSTreeEntry{split_line[1], file_info_section.back()});
            }

            return ret;
        }

        return nullopt;
    }
}
