#include <vcpkg/base/expected.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h>

namespace
{
    using namespace vcpkg;

    //     DECLARE_AND_REGISTER_MESSAGE(GitUnexpectedCommandOutput, (), "", "unexpected git output");
    //     DECLARE_AND_REGISTER_MESSAGE(GitStatusUnknownFileStatus,
    //                                  (msg::value),
    //                                  "{value} is a single character indicating file status, for example: A, U, M, D",
    //                                  "unknown file status: {value}");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedNewLine, (), "", "expected new line");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedPath, (), "", "expected a path");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedNewLineOrPath, (), "", "expected new line or path");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedValue, (), "", "expected a value");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedGitObjectType, (), "", "expected one a valid type git object type");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedGitObject, (), "", "expected a Git SHA");
    //     DECLARE_AND_REGISTER_MESSAGE(GitFailedToInitializeLocalRepository,
    //                                  (msg::path),
    //                                  "",
    //                                  "failed to initialize local repository in {path}");
    //     DECLARE_AND_REGISTER_MESSAGE(GitFailedToFetchRefFromRepository,
    //                                  (msg::git_ref, msg::url),
    //                                  "",
    //                                  "failed to fetch ref {git_ref} from repository {url}");

    DECLARE_AND_REGISTER_MESSAGE(GitErrorWhileRemovingFiles, (msg::path), "", "failed to remove {path}");
    DECLARE_AND_REGISTER_MESSAGE(GitErrorCreatingDirectory, (msg::path), "", "failed to create {path}");
    //     DECLARE_AND_REGISTER_MESSAGE(GitCheckoutFailedToCreateArchive, (), "", "failed to create .tar file");
    DECLARE_AND_REGISTER_MESSAGE(GitErrorRenamingFile,
                                 (msg::old_value, msg::new_value),
                                 "{old_value} is the original path of a renamed file, {new_value} is the new path",
                                 "failed to rename {old_value} to {new_value}");
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

    ExpectedL<std::vector<GitStatusLine>> parse_git_status_output(StringView output)
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
        ParserBase parser(output, "git status");
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
                        parser.add_error(msg::format(msgGitParseExpectedPath));
                        break;
                    }
                    auto path = parser.match_until(ParserBase::is_whitespace).to_string();
                    result.old_path = orig_path;
                    result.path = path;
                }
                else
                {
                    parser.add_error(msg::format(msgGitParseExpectedNewLineOrPath));
                    break;
                }
            }

            if (!ParserBase::is_lineend(parser.cur()))
            {
                parser.add_error(msg::format(msgGitParseExpectedNewLine));
                break;
            }

            parser.next();
            results.push_back(result);
        }

        if (auto error = parser.get_error())
        {
            return msg::format(msgGitUnexpectedCommandOutput).append_raw('\n').append_raw(error->to_string());
        }

        return results;
    }

    ExpectedL<std::vector<GitLsTreeLine>> parse_git_ls_tree_output(StringView output)
    {
        // Output of ls-tree is a list of git objects in the tree, each line is in the format
        //
        // MODE TYPE TREEISH    PATH
        //
        // https://git-scm.com/docs/git-ls-tree/
        static constexpr StringLiteral valid_types[]{
            "blob",
            "tree",
            "commit",
        };

        const auto extract_value = [](ParserBase& parser, std::string& into) -> bool {
            if (parser.is_whitespace(parser.cur()))
            {
                parser.add_error(msg::format(msgGitParseExpectedValue));
                return false;
            }

            into = parser.match_until(ParserBase::is_whitespace).to_string();
            parser.skip_tabs_spaces();
            return !into.empty();
        };

        std::vector<GitLsTreeLine> results;

        ParserBase parser(output, "git-ls-tree");
        while (!parser.at_eof())
        {
            GitLsTreeLine result;

            if (!(extract_value(parser, result.mode) && extract_value(parser, result.type) &&
                  extract_value(parser, result.git_object) && extract_value(parser, result.path)))
            {
                break;
            }

            if (std::find(std::begin(valid_types), std::end(valid_types), result.type) == std::end(valid_types))
            {
                parser.add_error(msg::format(msgGitParseExpectedGitObjectType));
                break;
            }

            if (result.git_object.size() != 40)
            {
                parser.add_error(msg::format(msgGitParseExpectedGitObject));
                break;
            }

            if (!parser.is_lineend(parser.cur()))
            {
                parser.add_error(msg::format(msgGitParseExpectedNewLine));
                break;
            }

            results.emplace_back(result);
            parser.next();
        }

        if (auto error = parser.get_error())
        {
            return msg::format_error(msgGitUnexpectedCommandOutput).append_raw("\n").append_raw(error->to_string());
        }

        return results;
    }

    static ExpectedL<std::string> execute_git_cmd(const Command& cmd)
    {
        return flatten_out(cmd_execute_and_capture_output(cmd), cmd.command_line());
    }

    static ExpectedL<Unit> execute_git_cmd_unit(const Command& cmd)
    {
        return flatten(cmd_execute_and_capture_output(cmd), cmd.command_line());
    }

    struct GitImpl final : IGit
    {
        explicit GitImpl(StringView sv) : git_exe(sv.to_string()) { }

        virtual ExpectedL<std::string> show_pretty_commit(const GitConfig& config, StringView rev) const override
        {
            auto cmd = git_cmd(config).string_arg("show").string_arg("--pretty=format:%h %cs (%cr)").string_arg(rev);
            return execute_git_cmd(cmd);
        }

        virtual ExpectedL<std::string> show(const GitConfig& config, StringView rev) const override
        {
            auto cmd = git_cmd(config).string_arg("show").string_arg(rev);
            return execute_git_cmd(cmd);
        }

        /// \param path An optional subpath to be queried
        virtual ExpectedL<std::vector<GitStatusLine>> status(const GitConfig& config,
                                                             StringView path = {}) const override
        {
            auto cmd = git_cmd(config).string_arg("status").string_arg("--porcelain=v1");
            if (!path.empty())
            {
                cmd.string_arg("--").string_arg(path);
            }
            return execute_git_cmd(cmd).then(parse_git_status_output);
        }

        /// If destination exists, immediately returns.
        /// \param rev Uses git revision syntax (e.g. <commit>[:<subpath>])
        ExpectedL<Unit> archive(const GitConfig& config, StringView rev, StringView destination) const
        {
            const auto cmd =
                git_cmd(config).string_arg("archive").string_arg(rev).string_arg("-o").string_arg(destination);
            return execute_git_cmd_unit(cmd);
        }

        ExpectedL<Unit> init(const GitConfig& config) const
        {
            const auto cmd = git_cmd(config).string_arg("init");
            return execute_git_cmd_unit(cmd);
        }

        // fetch a repository into the specified work tree
        // the directory pointed at by config.work_tree should already exist
        ExpectedL<Unit> fetch(const GitConfig& config, StringView uri, StringView ref) const
        {
            const auto cmd = git_cmd(config)
                                 .string_arg("fetch")
                                 .string_arg("--update-shallow")
                                 .string_arg("--force")
                                 .string_arg("--")
                                 .string_arg(uri)
                                 .string_arg(ref);
            return execute_git_cmd_unit(cmd);
        }

        virtual ExpectedL<std::string> rev_parse(const GitConfig& config, StringView rev) const override
        {
            auto cmd = git_cmd(config).string_arg("rev-parse").string_arg(rev);
            return execute_git_cmd(cmd).map([](const std::string& s) { return Strings::trim(s).to_string(); });
        }

        virtual ExpectedL<std::vector<GitLogResult>> log(const GitConfig& config, StringView path) const override
        {
            auto cmd = git_cmd(config)
                           .string_arg("log")
                           .string_arg("--format=%H %cd")
                           .string_arg("--date=short")
                           .string_arg("--left-only")
                           .string_arg("--")
                           .string_arg(path);
            return execute_git_cmd(cmd).map([](const std::string& output) {
                return Util::fmap(Strings::split(output, '\n'), [](const std::string& line) {
                    auto pos = line.find(' ');
                    return GitLogResult{line.substr(0, pos), pos != std::string::npos ? line.substr(pos + 1) : ""};
                });
            });
        }

        virtual ExpectedL<Unit> checkout(const GitConfig& config, StringView rev, View<StringView> files) const override
        {
            auto cmd = git_cmd(config)
                           .string_arg("checkout")
                           .string_arg(rev)
                           .string_arg("-f")
                           .string_arg("-q")
                           .string_arg("--");
            for (auto file : files)
            {
                cmd.string_arg(file);
            }
            return execute_git_cmd_unit(cmd);
        }

        virtual ExpectedL<Unit> reset(const GitConfig& config) const override
        {
            auto cmd = git_cmd(config).string_arg("reset");
            return execute_git_cmd_unit(cmd);
        }

        virtual ExpectedL<bool> is_commit(const GitConfig& config, StringView rev) const override
        {
            auto cmd = git_cmd(config).string_arg("cat-file").string_arg("-t").string_arg(rev);
            return execute_git_cmd(cmd).map([](const std::string& s) { return s == "commit\n"; });
        }

        virtual ExpectedL<std::vector<GitLsTreeLine>> ls_tree(const GitConfig& config,
                                                              StringView ref,
                                                              GitLsTreeOptions opts) const override
        {
            auto cmd = git_cmd(config).string_arg("ls-tree").string_arg(ref);

            if (opts.recursive)
            {
                cmd.string_arg("-r");
            }

            if (opts.dirs_only)
            {
                cmd.string_arg("-d");
            }

            if (!opts.path.empty())
            {
                cmd.string_arg("--").string_arg(opts.path);
            }
            return execute_git_cmd(cmd).then(parse_git_ls_tree_output);
        }

        virtual ExpectedL<std::string> ls_remote(const GitConfig& config, StringView uri, StringView ref) const override
        {
            auto cmd = git_cmd(config).string_arg("ls-remote").string_arg(uri).string_arg(ref);

            return execute_git_cmd(cmd).then([&cmd](StringView output) -> ExpectedL<std::string> {
                auto it = std::find(output.begin(), output.end(), ' ');
                if (it == output.end())
                {
                    return msg::format(msgGitCommandFailed, msg::command_line = cmd.command_line())
                        .append_raw('\n')
                        .append_raw(output);
                }
                else
                {
                    return StringView{output.begin(), it}.to_string();
                }
            });
        }

        virtual ExpectedL<std::string> init_fetch(const GitConfig& config,
                                                  Filesystem& fs,
                                                  StringView uri,
                                                  StringView ref) const override
        {
            // If a fetch has occurred or is occurring, we can skip initialization and locking.
            if (!fs.exists(config.git_dir / "FETCH_HEAD", IgnoreErrors{}))
            {
                fs.create_directories(config.git_work_tree, VCPKG_LINE_INFO);
                const auto lock_file = config.git_work_tree / ".vcpkg-lock";
                auto guard = fs.take_exclusive_file_lock(lock_file, IgnoreErrors{});

                auto maybe_init = init(config);
                if (!maybe_init)
                {
                    return maybe_init.error();
                }
            }

            // Fetch to a unique ref owned by us to avoid race conditions.
            const auto fetch_head_procid = Strings::concat("refs/heads/FETCH_HEAD_", get_process_id());
            auto maybe_fetch = fetch(config, uri, Strings::concat(ref, ":", fetch_head_procid));
            if (!maybe_fetch)
            {
                return maybe_fetch.error();
            }

            return rev_parse(config, fetch_head_procid);
        }

        virtual ExpectedL<Path> splat_object(const GitConfig& config,
                                             Filesystem& fs,
                                             const Path& cmake_exe,
                                             const Path& destination,
                                             StringView git_object) const override
        {
            if (fs.exists(destination, IgnoreErrors{}))
            {
                return destination;
            }

            const auto pid = get_process_id();
            const auto destination_tmp = fmt::format("{}.{}.tmp", destination.generic_u8string(), pid);
            const auto destination_tar = fmt::format("{}.{}.tmp.tar", destination.generic_u8string(), pid);

            std::error_code ec;
            Path failure_point;
            fs.remove_all(destination_tmp, ec, failure_point);
            if (ec)
            {
                // failed to remove {failure_point}
                return msg::format(msgGitErrorWhileRemovingFiles, msg::path = failure_point)
                    .append_raw('\n')
                    .append_raw(ec.message());
            }

            fs.create_directories(destination_tmp, ec);
            if (ec)
            {
                // failed to create {destination_tmp}
                return msg::format(msgGitErrorCreatingDirectory, msg::path = destination_tmp)
                    .append_raw('\n')
                    .append_raw(ec.message());
            }

            auto maybe_tar = archive(config, git_object, destination_tar);
            if (!maybe_tar)
            {
                return std::move(maybe_tar).error();
            }

            extract_tar_cmake(cmake_exe, destination_tar, destination_tmp);
            fs.remove(destination_tar, ec);
            if (ec)
            {
                // failed to remove {failure_point}
                return msg::format(msgGitErrorWhileRemovingFiles, msg::path = destination_tar)
                    .append_raw('\n')
                    .append_raw(ec.message());
            }

            fs.rename_with_retry(destination_tmp, destination, ec);
            if (ec)
            {
                // failed to rename {destination_tmp} to {destination}
                return msg::format(
                           msgGitErrorRenamingFile, msg::old_value = destination_tmp, msg::new_value = destination)
                    .append_raw('\n')
                    .append_raw(ec.message());
            }

            return destination;
        }

        Command git_cmd(const GitConfig& config) const
        {
            auto cmd = Command(git_exe);
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

        const std::string git_exe;
    };
    std::unique_ptr<IGit> make_git_from_exe(StringView git_exe) { return std::make_unique<GitImpl>(git_exe); }
}
