#include <vcpkg/base/expected.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
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
    std::string try_extract_port_name_from_path(StringView path)
    {
        static constexpr StringLiteral prefix = "ports/";
        static constexpr size_t min_path_size = sizeof("ports/*/") - 1;
        if (path.size() >= min_path_size && Strings::starts_with(path, prefix))
        {
            auto no_prefix = path.substr(prefix.size());
            auto slash = std::find(no_prefix.begin(), no_prefix.end(), '/');
            if (slash != no_prefix.end())
            {
                return std::string(no_prefix.begin(), slash);
            }
        }
        return {};
    }

    Optional<std::vector<GitStatusLine>> parse_git_status_output(DiagnosticContext& context,
                                                                 StringView output,
                                                                 StringView cmd_line)
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
                    parser.add_error(msg::format(msgGitStatusUnknownFileStatus, msg::value = static_cast<char>(c)));
                    into = Status::Unknown;
            }
            parser.next();
            return Status::Unknown != into;
        };

        Optional<std::vector<GitStatusLine>> results_storage;
        auto& results = results_storage.emplace();
        ParserBase parser(context, output, "git status", 0);
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
                        parser.add_error(msg::format(msgGitStatusOutputExpectedFileName));
                        results_storage.clear();
                        return results_storage;
                    }

                    auto path = parser.match_until(ParserBase::is_whitespace).to_string();
                    result.old_path = orig_path;
                    result.path = path;
                }
                else
                {
                    parser.add_error(msg::format(msgGitStatusOutputExpectedRenameOrNewline));
                    results_storage.clear();
                    return results_storage;
                }
            }

            if (!ParserBase::is_lineend(parser.cur()))
            {
                parser.add_error(msg::format(msgGitStatusOutputExpectedNewLine));
                results_storage.clear();
                return results_storage;
            }

            parser.next();
            results.push_back(result);
        }

        if (parser.any_errors())
        {
            context.report(DiagnosticLine{DiagKind::Note,
                                          msg::format(msgGitUnexpectedCommandOutputCmd, msg::command_line = cmd_line)});
            results_storage.clear();
        }

        return results_storage;
    }

    ExpectedL<std::vector<GitStatusLine>> parse_git_status_output(StringView git_status_output,
                                                                  StringView git_command_line)
    {
        return adapt_context_to_expected(
            static_cast<Optional<std::vector<GitStatusLine>> (*)(DiagnosticContext&, StringView, StringView)>(
                parse_git_status_output),
            git_status_output,
            git_command_line);
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

    ExpectedL<std::set<std::string>> git_ports_with_uncommitted_changes(const GitConfig& config)
    {
        auto maybe_results = git_status(config, "ports");
        if (auto results = maybe_results.get())
        {
            std::set<std::string> ret;
            for (auto&& result : *results)
            {
                auto&& port_name = try_extract_port_name_from_path(result.path);
                if (!port_name.empty())
                {
                    ret.emplace(port_name);
                }
            }
            return ret;
        }
        return std::move(maybe_results).error();
    }

    ExpectedL<bool> is_shallow_clone(const GitConfig& config)
    {
        return flatten_out(cmd_execute_and_capture_output(
                               git_cmd_builder(config).string_arg("rev-parse").string_arg("--is-shallow-repository")),
                           Tools::GIT)
            .map([](std::string&& output) { return "true" == Strings::trim(std::move(output)); });
    }
}
