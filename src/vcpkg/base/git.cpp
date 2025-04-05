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

#include <algorithm>

// When making changes to this file, check that the git command lines intended do what is expected on
// vcpkg's current minimum supported git version (2.7.4). You can get a version of git that old with docker:
//
// docker run -it --rm ubuntu:16.04
// apt update
// apt install git
// git --version # check that this is 2.7.4

namespace
{
    using namespace vcpkg;

    Command make_git_command(const vcpkg::Path& git_exe, GitRepoLocator locator, View<StringView> additional_args)
    {
        static constexpr StringLiteral DashC{"-C"};
        static constexpr StringLiteral GitDir{"--git-dir"};

        Command result{git_exe};
        const StringLiteral* arg_name;
        switch (locator.kind)
        {
            case GitRepoLocatorKind::CurrentDirectory: arg_name = &DashC; break;
            case GitRepoLocatorKind::DotGitDir: arg_name = &GitDir; break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
        result.string_arg(*arg_name);
        result.string_arg(locator.path);
        result.string_arg("-c").string_arg("core.autocrlf=false");
        for (auto&& additional_arg : additional_args)
        {
            result.string_arg(additional_arg);
        }

        return result;
    }

    Optional<std::string> run_cmd_trim(vcpkg::DiagnosticContext& context,
                                       const Command& command,
                                       const RedirectedProcessLaunchSettings& launch_settings)
    {
        auto maybe_result = cmd_execute_and_capture_output(context, command, launch_settings);
        if (auto prefix_output = check_zero_exit_code(context, command, maybe_result))
        {
            Strings::inplace_trim_end(*prefix_output);
            return std::move(*prefix_output);
        }

        return nullopt;
    }

    Optional<std::string> run_cmd_trim(vcpkg::DiagnosticContext& context, const Command& command)
    {
        RedirectedProcessLaunchSettings launch_settings;
        return run_cmd_trim(context, command, launch_settings);
    }

    Optional<std::string> run_cmd_git_with_index(DiagnosticContext& context,
                                                 const Command& command,
                                                 const Path& index_file)
    {
        RedirectedProcessLaunchSettings launch_settings;
        auto& environment = launch_settings.environment.emplace();
        environment.add_entry("GIT_INDEX_FILE", index_file);
        return run_cmd_trim(context, command, launch_settings);
    }
}

namespace vcpkg
{
    GitLSTreeEntry::GitLSTreeEntry(StringLiteral file_name, StringLiteral git_tree_sha)
        : file_name(file_name.data(), file_name.size()), git_tree_sha(git_tree_sha.data(), git_tree_sha.size())
    {
    }

    GitLSTreeEntry::GitLSTreeEntry(std::string&& file_name, std::string&& git_tree_sha)
        : file_name(std::move(file_name)), git_tree_sha(std::move(git_tree_sha))
    {
    }

    bool operator==(const GitLSTreeEntry& lhs, const GitLSTreeEntry& rhs) noexcept
    {
        return lhs.file_name == rhs.file_name && lhs.git_tree_sha == rhs.git_tree_sha;
    }

    bool operator!=(const GitLSTreeEntry& lhs, const GitLSTreeEntry& rhs) noexcept { return !(lhs == rhs); }

    bool operator==(const GitDiffTreeLine& lhs, const GitDiffTreeLine& rhs) noexcept
    {
        return lhs.old_mode == rhs.old_mode && lhs.new_mode == rhs.new_mode && lhs.old_sha == rhs.old_sha &&
               lhs.new_sha == rhs.new_sha && lhs.kind == rhs.kind && lhs.score == rhs.score &&
               lhs.file_name == rhs.file_name && lhs.old_file_name == rhs.old_file_name;
    }

    bool operator!=(const GitDiffTreeLine& lhs, const GitDiffTreeLine& rhs) noexcept { return !(lhs == rhs); }

    bool is_git_mode(StringView sv) noexcept
    {
        return sv.size() == 6 &&
               std::all_of(sv.begin(), sv.end(), [](char ch) noexcept { return '0' <= ch && ch <= '7'; });
    }

    bool is_git_sha(StringView sv) noexcept
    {
        return sv.size() == 40 && std::all_of(sv.begin(), sv.end(), [](char ch) noexcept {
                   return ('0' <= ch && ch <= '9') || ('a' <= ch && ch <= 'f');
               });
    }

    Optional<bool> is_shallow_clone(DiagnosticContext& context, const Path& git_exe, GitRepoLocator locator)
    {
        // --is-shallow-repository is not present in git 2.7.4, but in that case git just prints
        // "--is-shallow-repository", which is not "true", so we safely report "false"
        static constexpr StringView args[] = {StringLiteral{"rev-parse"}, StringLiteral{"--is-shallow-repository"}};
        return run_cmd_trim(context, make_git_command(git_exe, locator, args)).map([](std::string&& output) {
            return output == "true";
        });
    }

    Optional<std::string> git_prefix(DiagnosticContext& context, const Path& git_exe, const Path& target)
    {
        static constexpr StringView args[] = {StringLiteral{"rev-parse"}, StringLiteral{"--show-prefix"}};
        return run_cmd_trim(
            context, make_git_command(git_exe, GitRepoLocator{GitRepoLocatorKind::CurrentDirectory, target}, args));
    }

    Optional<Path> git_index_file(DiagnosticContext& context,
                                  const Filesystem& fs,
                                  const Path& git_exe,
                                  GitRepoLocator locator)
    {
        // We can't use --path-format unconditionally as that is unavailable in git 2.7.4.
        // However, the path format always appears to be absolute if we use --git-dir with an absolute path
        static constexpr StringView args[] = {
            StringLiteral{"rev-parse"}, StringLiteral{"--git-path"}, StringLiteral{"index"}};
        return git_absolute_git_dir(context, fs, git_exe, locator).then([&](Path&& absolute_git_dir) {
            auto git_path_index_cmd =
                make_git_command(git_exe, GitRepoLocator{GitRepoLocatorKind::DotGitDir, absolute_git_dir}, args);
            return run_cmd_trim(context, git_path_index_cmd).then([&](std::string&& proto_path) -> Optional<Path> {
                Optional<Path> result;
                auto& result_path = result.emplace(std::move(proto_path));
                if (!result_path.is_absolute() || !fs.exists(result_path, IgnoreErrors{}))
                {
                    context.report_error_with_log(result_path.native(),
                                                  msgGitUnexpectedCommandOutputCmd,
                                                  msg::command_line = git_path_index_cmd.command_line());
                    result.clear();
                }

                return result;
            });
        });
    }

    Optional<Path> git_absolute_git_dir(DiagnosticContext& context,
                                        const Filesystem& fs,
                                        const Path& git_exe,
                                        GitRepoLocator locator)
    {
        static constexpr StringView args[] = {StringLiteral{"rev-parse"}, StringLiteral{"--git-dir"}};
        switch (locator.kind)
        {
            case GitRepoLocatorKind::CurrentDirectory:
                return run_cmd_trim(context, make_git_command(git_exe, locator, args))
                    .then([&](std::string&& proto_path) { return fs.absolute(context, locator.path / proto_path); });
            case GitRepoLocatorKind::DotGitDir: return fs.absolute(context, locator.path);
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    bool git_add_with_index(DiagnosticContext& context, const Path& git_exe, const Path& target, const Path& index_file)
    {
        static constexpr StringView args[] = {StringLiteral{"add"}, StringLiteral{"-A"}, StringLiteral{"."}};
        return run_cmd_git_with_index(
                   context,
                   make_git_command(git_exe, GitRepoLocator{GitRepoLocatorKind::CurrentDirectory, target}, args),
                   index_file)
            .has_value();
    }

    Optional<std::string> git_write_index_tree(DiagnosticContext& context,
                                               const Path& git_exe,
                                               GitRepoLocator locator,
                                               const Path& index_file)
    {
        static constexpr StringView args[] = {StringLiteral{"write-tree"}};
        return run_cmd_git_with_index(context, make_git_command(git_exe, locator, args), index_file);
    }

    bool parse_git_ls_tree_output(DiagnosticContext& context,
                                  std::vector<GitLSTreeEntry>& target,
                                  StringView ls_tree_output,
                                  StringView ls_tree_command)
    {
        const auto lines = Strings::split(ls_tree_output, '\0');
        // The first line of the output is always the parent directory itself.
        for (auto&& line : lines)
        {
            // The default output comes in the format:
            // <mode> SP <type> SP <object> TAB <file>
            auto split_line = Strings::split(line, '\t');
            if (split_line.size() != 2)
            {
                context.report_error_with_log(
                    ls_tree_output, msgGitUnexpectedCommandOutputCmd, msg::command_line = ls_tree_command);
                return true;
            }

            auto file_info_section = Strings::split(split_line[0], ' ');
            if (file_info_section.size() != 3)
            {
                context.report_error_with_log(
                    ls_tree_output, msgGitUnexpectedCommandOutputCmd, msg::command_line = ls_tree_command);
                return true;
            }

            target.emplace_back(std::move(split_line[1]), std::move(file_info_section.back()));
        }

        return false;
    }

    Optional<std::vector<GitLSTreeEntry>> git_ls_tree(DiagnosticContext& context,
                                                      const Path& git_exe,
                                                      GitRepoLocator locator,
                                                      StringView treeish)
    {
        RedirectedProcessLaunchSettings launch_settings;
        launch_settings.encoding = Encoding::Utf8WithNulls;
        Optional<std::vector<GitLSTreeEntry>> result;

        StringView args[] = {StringLiteral{"ls-tree"}, treeish, StringLiteral{"--full-tree"}, StringLiteral{"-z"}};
        auto cmd = make_git_command(git_exe, locator, args);
        auto maybe_ls_tree_result = run_cmd_trim(context, cmd, launch_settings);
        if (auto ls_tree_output = maybe_ls_tree_result.get())
        {
            if (parse_git_ls_tree_output(context, result.emplace(), *ls_tree_output, cmd.command_line()))
            {
                result.clear();
            }

            return result;
        }

        return result;
    }

    bool git_extract_tree(DiagnosticContext& context,
                          const Filesystem& fs,
                          const Path& git_exe,
                          GitRepoLocator locator,
                          const Path& destination,
                          StringView treeish)
    {
        auto pid = get_process_id();
        Path git_tree_temp = fmt::format("{}_{}.tmp", destination, pid);
        git_tree_temp.make_generic();
        Path git_tree_index = fmt::format("{}_{}.index", destination, pid);
        auto parent = destination.parent_path();
        if (!parent.empty())
        {
            if (!fs.create_directories(context, parent).has_value())
            {
                return false;
            }
        }

        if (!fs.remove_all(context, git_tree_temp) || !fs.create_directory(context, git_tree_temp).has_value())
        {
            return false;
        }

        StringView read_tree_args[] = {StringLiteral{"read-tree"}, treeish};
        auto read_tree_cmd = make_git_command(git_exe, locator, read_tree_args);
        if (run_cmd_git_with_index(context, read_tree_cmd, git_tree_index).has_value())
        {
            // No --ignore-skip-worktree-bits because that was added in recent-ish git versions
            auto prefix_arg = fmt::format("--prefix={}/", git_tree_temp);
            StringView checkout_index_args[] = {StringLiteral{"--work-tree"},
                                                git_tree_temp,
                                                StringLiteral{"checkout-index"},
                                                StringLiteral{"-af"},
                                                prefix_arg};
            auto checkout_index_cmd = make_git_command(git_exe, locator, checkout_index_args);
            bool succeeded = run_cmd_git_with_index(context, checkout_index_cmd, git_tree_index).has_value();
            fs.remove(git_tree_index, IgnoreErrors{});
            return succeeded && fs.rename_or_delete(context, git_tree_temp, destination).has_value();
        }

        if (is_shallow_clone(null_diagnostic_context, git_exe, locator).value_or(false))
        {
            context.report(DiagnosticLine{DiagKind::Note, locator.path, msg::format(msgShallowRepositoryDetected)});
        }

        return false;
    }

    Optional<bool> git_check_is_commit(DiagnosticContext& context,
                                       const Path& git_exe,
                                       GitRepoLocator locator,
                                       StringView git_commit_id)
    {
        StringView args[] = {StringLiteral{"cat-file"}, StringLiteral{"-t"}, git_commit_id};
        return run_cmd_trim(context, make_git_command(git_exe, locator, args)).map([](std::string&& output) {
            return output == "commit";
        });
    }

    Optional<std::string> git_merge_base(
        DiagnosticContext& context, const Path& git_exe, GitRepoLocator locator, StringView commit1, StringView commit2)
    {
        StringView args[] = {StringLiteral{"merge-base"}, commit1, commit2};
        auto cmd = make_git_command(git_exe, locator, args);
        auto maybe_merge_base_output = run_cmd_trim(context, cmd);
        if (auto output = maybe_merge_base_output.get())
        {
            if (is_git_sha(*output))
            {
                return std::move(*output);
            }

            context.report_error_with_log(
                *output, msgGitUnexpectedCommandOutputCmd, msg::command_line = cmd.command_line());
        }

        return nullopt;
    }

    const char* parse_git_diff_tree_line(std::vector<GitDiffTreeLine>& target, const char* first, const char* last)
    {
        // from https://git-scm.com/docs/git-diff-tree#_raw_output_format
        // in-place edit  :100644 100644 bcd1234 0123456 M\0file0\0
        // copy-edit      :100644 100644 abcd123 1234567 C68\0file1\0file2\0
        // rename-edit    :100644 100644 abcd123 1234567 R86\0file1\0file3\0
        // create         :000000 100644 0000000 1234567 A\0file4\0
        // delete         :100644 000000 1234567 0000000 D\0file5\0
        // unmerged       :000000 000000 0000000 0000000 U\0file6\0
        // That is, from the left to the right:
        //
        // 1. a colon.
        // 2. mode for "src"; 000000 if creation or unmerged.
        // 3. space.
        // 4. mode for "dst"; 000000 if deletion or unmerged.
        // 5. a space.
        // 6. sha1 for "src"; 0{40} if creation or unmerged.
        // 7. a space.
        // 8. sha1 for "dst"; 0{40} if deletion, unmerged or "work tree out of sync with the index".
        // 9. a space.
        // 10. status, followed by optional "score" number.
        // 11. a tab or a NUL when -z option is used.
        // 12. path for "src"
        // 13. a tab or a NUL when -z option is used; only exists for C or R.
        // 14. path for "dst"; only exists for C or R.
        // 15. an LF or a NUL when -z option is used, to terminate the record.

        first = std::find(first, last, ':');
        static constexpr ptrdiff_t minimum_prefix_size = 1 + 7 + 7 + 41 + 41 + 2;
        if ((last - first) < minimum_prefix_size || first[0] != ':' || first[7] != ' ' || first[14] != ' ' ||
            first[55] != ' ' || first[96] != ' ')
        {
            return nullptr;
        }

        ++first; // skip :
        StringView old_mode{first, 6};
        if (!is_git_mode(old_mode))
        {
            return nullptr;
        }

        first += 7; // +1 to skip space, ditto below
        StringView new_mode{first, 6};
        if (!is_git_mode(new_mode))
        {
            return nullptr;
        }

        first += 7;
        StringView old_sha{first, 40};
        if (!is_git_sha(old_sha))
        {
            return nullptr;
        }

        first += 41;
        StringView new_sha{first, 40};
        if (!is_git_sha(new_sha))
        {
            return nullptr;
        }

        first += 41;
        bool has_second_file;
        GitDiffTreeLineKind kind;
        switch (*first)
        {
            case 'A':
                has_second_file = false;
                kind = GitDiffTreeLineKind::Added;
                break;
            case 'C':
                has_second_file = true;
                kind = GitDiffTreeLineKind::Copied;
                break;
            case 'D':
                has_second_file = false;
                kind = GitDiffTreeLineKind::Deleted;
                break;
            case 'M':
                has_second_file = false;
                kind = GitDiffTreeLineKind::Modified;
                break;
            case 'R':
                has_second_file = true;
                kind = GitDiffTreeLineKind::Renamed;
                break;
            case 'T':
                has_second_file = false;
                kind = GitDiffTreeLineKind::TypeChange;
                break;
            case 'U':
                has_second_file = false;
                kind = GitDiffTreeLineKind::Unmerged;
                break;
            case 'X':
                has_second_file = false;
                kind = GitDiffTreeLineKind::Unknown;
                break;
            default: return nullptr;
        }

        int score;
        ++first;
        static constexpr auto is_tab_or_nul = [](char ch) noexcept { return ch == '\0' || ch == '\t'; };
        if (first != last && !is_tab_or_nul(*first))
        {
            auto score_end = std::find_if(first, last, is_tab_or_nul);
            if (score_end == last)
            {
                return nullptr;
            }

            auto maybe_score = Strings::strto<int>(StringView{first, score_end});
            if (auto p_score = maybe_score.get())
            {
                score = *p_score;
            }
            else
            {
                return nullptr;
            }

            first = score_end;
        }
        else
        {
            score = 0;
        }

        ++first; // skip tab or nul
        const char* old_file_start;
        size_t old_file_size;
        if (has_second_file)
        {
            auto old_file_end = std::find_if(first, last, is_tab_or_nul);
            if (old_file_end == last)
            {
                return nullptr;
            }

            old_file_start = first;
            old_file_size = old_file_end - first;
            first = old_file_end;
            ++first; // skip tab or nul
        }
        else
        {
            old_file_start = nullptr;
            old_file_size = 0;
        }

        static constexpr auto is_lf_or_nul = [](char ch) noexcept { return ch == '\0' || ch == '\n'; };
        auto file_end = std::find_if(first, last, is_lf_or_nul);
        if (file_end == last)
        {
            return nullptr;
        }

        StringView file_name{first, file_end};
        first = file_end;
        ++first; // skip lf or nul

        GitDiffTreeLine& entry = target.emplace_back();
        entry.old_mode.assign(old_mode.data(), old_mode.size());
        entry.new_mode.assign(new_mode.data(), new_mode.size());
        entry.old_sha.assign(old_sha.data(), old_sha.size());
        entry.new_sha.assign(new_sha.data(), new_sha.size());
        entry.kind = kind;
        entry.score = score;
        entry.file_name.assign(file_name.data(), file_name.size());
        entry.old_file_name.assign(old_file_start, old_file_size);
        return first;
    }

    Optional<std::vector<GitDiffTreeLine>> parse_git_diff_tree_lines(DiagnosticContext& context,
                                                                     StringView command_line,
                                                                     StringView output)
    {
        Optional<std::vector<GitDiffTreeLine>> result_storage;
        auto& result = result_storage.emplace();
        const char* first = output.begin();
        const char* const last = output.end();
        for (;;)
        {
            if (first == last)
            {
                return result_storage;
            }

            first = parse_git_diff_tree_line(result, first, last);
            if (!first)
            {
                context.report_error_with_log(
                    output, msgGitUnexpectedCommandOutputCmd, msg::command_line = command_line);
                result_storage.clear();
                return result_storage;
            }
        }
    }

    Optional<std::vector<GitDiffTreeLine>> git_diff_tree(
        DiagnosticContext& context, const Path& git_exe, GitRepoLocator locator, StringView tree1, StringView tree2)
    {
        RedirectedProcessLaunchSettings launch_settings;
        launch_settings.encoding = Encoding::Utf8WithNulls;
        StringView args[] = {StringLiteral{"diff-tree"}, StringLiteral{"-z"}, tree1, tree2};
        auto cmd = make_git_command(git_exe, locator, args);
        auto maybe_git_diff_tree_output = run_cmd_trim(context, cmd, launch_settings);
        if (auto git_diff_tree_output = maybe_git_diff_tree_output.get())
        {
            return parse_git_diff_tree_lines(context, cmd.command_line(), *git_diff_tree_output);
        }
        return nullopt;
    }
}
