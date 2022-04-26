#include <vcpkg/base/expected.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.process.h>

namespace
{
    using namespace vcpkg;

    DECLARE_AND_REGISTER_MESSAGE(GitCommandFailed, (msg::command_line), "", "failed to execute: {command_line}");
    DECLARE_AND_REGISTER_MESSAGE(GitUnexpectedCommandOutput, (), "", "unexpected git output");
    DECLARE_AND_REGISTER_MESSAGE(GitStatusUnknownFileStatus,
                                 (msg::value),
                                 "{value} is a single character indicating file status, e.g.: A, U, M, D",
                                 "unknown file status: {value}");
    DECLARE_AND_REGISTER_MESSAGE(GitStatusOutputExpectedNewLine, (), "", "expected new line");
    DECLARE_AND_REGISTER_MESSAGE(GitStatusOutputExpectedRenameOrNewline, (), "", "expected renamed file or new lines");

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
        using std::begin;
        using std::end;

        static constexpr StringLiteral prefix = "ports/";
        static constexpr size_t min_path_size = sizeof("ports/*/") - 1;
        if (path.size() >= min_path_size && Strings::starts_with(path, prefix))
        {
            using std::begin;
            using std::end;

            auto no_prefix = path.substr(prefix.size());
            auto slash = Strings::find_first_of(no_prefix, "/");
            if (slash != no_prefix.end())
            {
                return std::string(no_prefix.begin(), slash);
            }
        }
        return {};
    }

    ExpectedL<std::vector<GitStatusLine>> parse_git_status_output(StringView output)
    {
        // Output of git status --porcelain=v1 is in the form:
        //
        // XY PATH
        // or
        // XY ORIG_PATH -> PATH
        //
        // X: is the status on the index
        // Y: is the status on the work tree
        // ORIG_PATH: is the original filepath for rename operations
        // PATH: is the path of the modified file
        static constexpr StringLiteral RENAME_TOKEN = "->";
        static constexpr auto skip_whitespace_but_no_lineend = [](char32_t c) { return c == ' ' || c == '\t'; };

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
        ParserBase parser(output, "git status");
        for (;;)
        {
            if (parser.at_eof())
            {
                break;
            }

            GitStatusLine result;
            if (!extract_status(parser, result.index_status) || !extract_status(parser, result.work_tree_status))
            {
                break;
            }
            parser.skip_whitespace();
            auto orig_path = parser.match_until(ParserBase::is_whitespace).to_string();
            parser.match_while(skip_whitespace_but_no_lineend);
            if (ParserBase::is_lineend(parser.cur()))
            {
                result.path = orig_path;
            }
            else if (parser.try_match_keyword(RENAME_TOKEN))
            {
                parser.skip_whitespace();
                auto path = parser.match_until(ParserBase::is_whitespace).to_string();
                result.original_path = orig_path;
                result.path = path;
                parser.match_while(skip_whitespace_but_no_lineend);
            }
            else
            {
                parser.add_error(msg::format(msgGitStatusOutputExpectedRenameOrNewline), parser.cur_loc());
                break;
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
            return msg::format(msgGitUnexpectedCommandOutput).append_raw(error->format());
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
        auto output = cmd_execute_and_capture_output(cmd);
        if (output.exit_code != 0)
        {
            return msg::format(msgGitCommandFailed, msg::command_line = cmd.command_line())
                .appendnl()
                .append_raw(output.output);
        }
        return parse_git_status_output(output.output);
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
        return std::move(maybe_results.error());
    }
}
