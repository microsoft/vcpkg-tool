#include <vcpkg/base/expected.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h>

namespace
{
    using namespace vcpkg;

    DECLARE_AND_REGISTER_MESSAGE(GitCommandFailed, (msg::command_line), "", "failed to execute: {command_line}");
    DECLARE_AND_REGISTER_MESSAGE(GitUnexpectedCommandOutput, (), "", "unexpected git output");
    DECLARE_AND_REGISTER_MESSAGE(GitStatusUnknownFileStatus,
                                 (msg::value),
                                 "{value} is a single character indicating file status, for example: A, U, M, D",
                                 "unknown file status: {value}");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedNewLine, (), "", "expected new line");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedPath, (), "", "expected a path");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedNewLineOrPath, (), "", "expected new line or path");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedValue, (), "", "expected a value");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedGitObjectType, (), "", "expected one a valid type git object type");
    DECLARE_AND_REGISTER_MESSAGE(GitParseExpectedGitObject, (), "", "expected a Git SHA");
    DECLARE_AND_REGISTER_MESSAGE(GitFailedToInitializeLocalRepository,
                                 (msg::path),
                                 "",
                                 "failed to initialize local repository in {path}");
    DECLARE_AND_REGISTER_MESSAGE(GitFailedToFetchRefFromRepository,
                                 (msg::git_ref, msg::url),
                                 "",
                                 "failed to fetch ref {git_ref} from repository {url}");
    DECLARE_AND_REGISTER_MESSAGE(GitCheckoutPortFailed,
                                 (msg::package_name, msg::git_ref, msg::path),
                                 "",
                                 "while checking out {package_name} with SHA {git_ref} into {path}");

    DECLARE_AND_REGISTER_MESSAGE(GitCheckoutRegistryPortFailed,
                                 (msg::git_ref, msg::path),
                                 "",
                                 "while checking out {git_ref} into {path}");
    DECLARE_AND_REGISTER_MESSAGE(GitErrorWhileRemovingFiles, (msg::path), "", "failed to remove {path}");
    DECLARE_AND_REGISTER_MESSAGE(GitErrorCreatingDirectory, (msg::path), "", "failed to create {path}");
    DECLARE_AND_REGISTER_MESSAGE(GitCheckoutFailedToCreateArchive, (), "", "failed to create .tar file");
    DECLARE_AND_REGISTER_MESSAGE(GitErrorRenamingFile,
                                 (msg::old_value, msg::new_value),
                                 "{old_value} is the original path of a renamed file, {new_value} is the new path",
                                 "failed to rename {old_value} to {new_value}");

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
                        parser.add_error(msg::format(msgGitParseExpectedPath), parser.cur_loc());
                        break;
                    }
                    auto path = parser.match_until(ParserBase::is_whitespace).to_string();
                    result.old_path = orig_path;
                    result.path = path;
                }
                else
                {
                    parser.add_error(msg::format(msgGitParseExpectedNewLineOrPath), parser.cur_loc());
                    break;
                }
            }

            if (!ParserBase::is_lineend(parser.cur()))
            {
                parser.add_error(msg::format(msgGitParseExpectedNewLine), parser.cur_loc());
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

    ExpectedL<bool> git_init(const GitConfig& config)
    {
        const auto cmd = git_cmd_builder(config).string_arg("init");
        const auto output = cmd_execute_and_capture_output(cmd);
        if (output.exit_code != 0)
        {
            // failed to initialize local repository in {work_tree}
            return msg::format(msgGitFailedToInitializeLocalRepository, msg::path = config.git_work_tree)
                .appendnl()
                .append_raw(output.output);
        }
        return true;
    }

    ExpectedL<bool> git_fetch(const GitConfig& config, StringView uri, StringView ref)
    {
        const auto cmd = git_cmd_builder(config)
                             .string_arg("fetch")
                             .string_arg("--update-shallow")
                             .string_arg("--")
                             .string_arg(uri)
                             .string_arg(ref);

        auto output = cmd_execute_and_capture_output(cmd);
        if (output.exit_code != 0)
        {
            // failed to fetch ref {treeish} from repository {url}
            return msg::format(msgGitFailedToFetchRefFromRepository, msg::git_ref = ref, msg::url = uri)
                .appendnl()
                .append_raw(output.output);
        }

        return true;
    }

    ExpectedL<std::string> git_rev_parse(const GitConfig& config, StringView ref, StringView path)
    {
        auto cmd = git_cmd_builder(config).string_arg("rev-parse");
        if (path.empty())
        {
            cmd.string_arg(ref);
        }
        else
        {
            cmd.string_arg(Strings::concat(ref, ":", path));
        }
        auto output = cmd_execute_and_capture_output(cmd);
        if (output.exit_code != 0)
        {
            // failed to execute {command_line} for repository
            return msg::format(msgGitCommandFailed, msg::command_line = cmd.command_line())
                .appendnl()
                .append_raw(output.output);
        }
        return Strings::trim(output.output).to_string();
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
                parser.add_error(msg::format(msgGitParseExpectedValue), parser.cur_loc());
                return false;
            }

            into = parser.match_until(ParserBase::is_whitespace).to_string();
            parser.skip_tabs_spaces();
            return !into.empty();
        };

        std::vector<GitLsTreeLine> results;

        ParserBase parser(output, "git ls-tree");
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
                parser.add_error(msg::format(msgGitParseExpectedGitObjectType), parser.cur_loc());
                break;
            }

            if (result.git_object.size() != 40)
            {
                parser.add_error(msg::format(msgGitParseExpectedGitObject), parser.cur_loc());
                break;
            }

            if (!parser.is_lineend(parser.cur()))
            {
                parser.add_error(msg::format(msgGitParseExpectedNewLine), parser.cur_loc());
                break;
            }

            results.emplace_back(result);
            parser.next();
        }

        if (auto error = parser.get_error())
        {
            return msg::format(msgGitUnexpectedCommandOutput).append_raw(error->format());
        }

        return results;
    }

    ExpectedL<std::vector<GitLsTreeLine>> git_ls_tree(
        const GitConfig& config, StringView ref, StringView path, Git::Recursive recursive, Git::DirsOnly dirs_only)
    {
        auto cmd = git_cmd_builder(config).string_arg("ls-tree").string_arg(ref);

        if (Util::Enum::to_bool(recursive))
        {
            cmd.string_arg("-r");
        }

        if (Util::Enum::to_bool(dirs_only))
        {
            cmd.string_arg("-d");
        }

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

        return parse_git_ls_tree_output(output.output);
    }

    ExpectedL<std::string> git_show(const GitConfig& config, StringView git_object, StringView path)
    {
        auto cmd = git_cmd_builder(config).string_arg("show");
        if (path.empty())
        {
            cmd.string_arg(git_object);
        }
        else
        {
            cmd.string_arg(Strings::concat(git_object, ":", path));
        }
        auto output = cmd_execute_and_capture_output(cmd);
        if (output.exit_code != 0)
        {
            return msg::format(msgGitCommandFailed, msg::command_line = cmd.command_line())
                .appendnl()
                .append_raw(output.output);
        }

        return std::move(output.output);
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

    ExpectedL<std::string> git_fetch_from_remote_registry(const GitConfig& config,
                                                          Filesystem& fs,
                                                          StringView uri,
                                                          StringView ref)
    {
        fs.create_directories(config.git_work_tree, VCPKG_LINE_INFO);
        const auto lock_file = config.git_work_tree / ".vcpkg-lock";
        auto guard = fs.take_exclusive_file_lock(lock_file, IgnoreErrors{});

        auto maybe_init = git_init(config);
        if (!maybe_init)
        {
            return maybe_init.error();
        }

        auto maybe_fetch = git_fetch(config, uri, ref);
        if (!maybe_fetch)
        {
            return maybe_fetch.error();
        }

        auto maybe_rev_parse = git_rev_parse(config, "FETCH_HEAD");
        if (!maybe_rev_parse)
        {
            return maybe_rev_parse.error();
        }

        return maybe_rev_parse.value_or_exit(VCPKG_LINE_INFO);
    }

    ExpectedL<std::string> git_current_sha(const GitConfig& config, Optional<std::string> maybe_embedded_sha)
    {
        if (auto sha = maybe_embedded_sha.get())
        {
            return *sha;
        }

        auto maybe_sha = git_rev_parse(config, "HEAD");
        if (auto sha = maybe_sha.get())
        {
            return *sha;
        }

        return maybe_sha.error();
    }

    ExpectedL<Path> archive_and_extract_object(
        const GitConfig& config, Filesystem& fs, const Path& cmake_exe, const Path& destination, StringView git_object)
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
                .appendnl()
                .append_raw(ec.message());
        }

        fs.create_directories(destination_tmp, ec);
        if (ec)
        {
            // failed to create {destination_tmp}
            return msg::format(msgGitErrorCreatingDirectory, msg::path = destination_tmp)
                .appendnl()
                .append_raw(ec.message());
        }

        const auto tar_cmd = git_cmd_builder(config)
                                 .string_arg("archive")
                                 .string_arg(git_object)
                                 .string_arg("-o")
                                 .string_arg(destination_tar);
        const auto tar_output = cmd_execute_and_capture_output(tar_cmd);
        if (tar_output.exit_code != 0)
        {
            // failed to create {destination_tmp}
            return msg::format(msgGitCheckoutFailedToCreateArchive).appendnl().append_raw(tar_output.output);
        }

        extract_tar_cmake(cmake_exe, destination_tar, destination_tmp);
        fs.remove(destination_tar, ec);
        if (ec)
        {
            // failed to remove {failure_point}
            return msg::format(msgGitErrorWhileRemovingFiles, msg::path = destination_tar)
                .appendnl()
                .append_raw(ec.message());
        }

        fs.rename_with_retry(destination_tmp, destination, ec);
        if (ec)
        {
            // failed to rename {destination_tmp} to {destination}
            return msg::format(msgGitErrorRenamingFile, msg::old_value = destination_tmp, msg::new_value = destination)
                .appendnl()
                .append_raw(ec.message());
        }

        return destination;
    }

    ExpectedL<Path> git_checkout_port(const GitConfig& config,
                                      Filesystem& fs,
                                      const Path& cmake_exe,
                                      const Path& containing_dir,
                                      StringView port_name,
                                      StringView git_object)
    {
        const auto destination = containing_dir / port_name / git_object;

        // while checking out {port_name} with SHA {git_object}
        auto error_prelude = msg::format(msgGitCheckoutPortFailed,
                                         msg::package_name = port_name,
                                         msg::git_ref = git_object,
                                         msg::path = destination)
                                 .appendnl();
        auto maybe_path = archive_and_extract_object(config, fs, cmake_exe, destination, git_object);
        if (auto path = maybe_path.get())
        {
            return *path;
        }

        return error_prelude.append(maybe_path.error());
    }

    ExpectedL<Path> git_checkout_registry_port(const GitConfig& config,
                                               Filesystem& fs,
                                               const Path& cmake_exe,
                                               const Path& containing_dir,
                                               StringView git_object)
    {
        const auto destination = containing_dir / git_object;
        auto error_prelude =
            msg::format(msgGitCheckoutRegistryPortFailed, msg::git_ref = git_object, msg::path = destination)
                .appendnl();
        auto maybe_path = archive_and_extract_object(config, fs, cmake_exe, destination, git_object);
        if (auto path = maybe_path.get())
        {
            return *path;
        }

        return error_prelude.append(maybe_path.error());
    }

    ExpectedL<std::unordered_map<std::string, std::string>> git_ports_tree_map(const GitConfig& config, StringView ref)
    {
        auto maybe_files = git_ls_tree(config, ref, "ports/", Git::Recursive::NO, Git::DirsOnly::YES);
        if (!maybe_files)
        {
            return maybe_files.error();
        }

        static constexpr StringLiteral prefix = "ports/";
        static constexpr size_t prefix_size = prefix.size() - 1;
        std::unordered_map<std::string, std::string> results;
        for (auto&& file : maybe_files.value_or_exit(VCPKG_LINE_INFO))
        {
            if (Strings::starts_with(file.path, prefix))
            {
                results.emplace(file.path.substr(0, prefix_size), file.git_object);
            }
        }
        return results;
    }
}
