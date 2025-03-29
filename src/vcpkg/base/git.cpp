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

namespace
{
    using namespace vcpkg;

    struct GitCmdResult
    {
        Command command;
        Optional<std::string> maybe_output;
    };

    GitCmdResult run_git_cmd(vcpkg::DiagnosticContext& context,
                             const vcpkg::Path& git_exe,
                             GitRepoLocator locator,
                             View<StringView> additional_args,
                             const RedirectedProcessLaunchSettings& launch_settings)
    {
        // Inline git_cmd_builder functionality
        static constexpr StringLiteral DashC{"-C"};
        static constexpr StringLiteral GitDir{"--git-dir"};

        GitCmdResult result{Command{git_exe}};
        const StringLiteral* arg_name;
        switch (locator.kind)
        {
            case GitRepoLocatorKind::CurrentDirectory: arg_name = &DashC; break;
            case GitRepoLocatorKind::DotGitDir: arg_name = &GitDir; break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
        result.command.string_arg(*arg_name);
        result.command.string_arg(locator.path);
        result.command.string_arg("-c").string_arg("core.autocrlf=false");
        for (auto&& additional_arg : additional_args)
        {
            result.command.string_arg(additional_arg);
        }

        auto maybe_result = cmd_execute_and_capture_output(context, result.command, launch_settings);
        if (auto prefix_output = check_zero_exit_code(context, result.command, maybe_result))
        {
            Strings::inplace_trim_end(*prefix_output);
            result.maybe_output.emplace(std::move(*prefix_output));
        }

        return result;
    }

    GitCmdResult run_git_cmd(DiagnosticContext& context,
                             const Path& git_exe,
                             GitRepoLocator locator,
                             View<StringView> additional_args)
    {
        RedirectedProcessLaunchSettings launch_settings;
        return run_git_cmd(context, git_exe, locator, additional_args, launch_settings);
    }

    GitCmdResult run_git_cmd_with_index(DiagnosticContext& context,
                                        const Path& git_exe,
                                        GitRepoLocator locator,
                                        const Path& index_file,
                                        View<StringView> additional_args)
    {
        RedirectedProcessLaunchSettings launch_settings;
        auto& environment = launch_settings.environment.emplace();
        environment.add_entry("GIT_INDEX_FILE", index_file);
        return run_git_cmd(context, git_exe, locator, additional_args, launch_settings);
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
        static constexpr StringView args[] = {StringLiteral{"rev-parse"}, StringLiteral{"--is-shallow-repository"}};
        return run_git_cmd(context, git_exe, locator, args).maybe_output.map([](std::string&& output) {
            return "true" == Strings::trim(std::move(output));
        });
    }

    Optional<std::string> git_prefix(DiagnosticContext& context, const Path& git_exe, const Path& target)
    {
        static constexpr StringView args[] = {StringLiteral{"rev-parse"}, StringLiteral{"--show-prefix"}};
        return run_git_cmd(context, git_exe, GitRepoLocator{GitRepoLocatorKind::CurrentDirectory, target}, args)
            .maybe_output;
    }

    Optional<Path> git_index_file(DiagnosticContext& context, const Path& git_exe, GitRepoLocator locator)
    {
        static constexpr StringView args[] = {StringLiteral{"rev-parse"},
                                              StringLiteral{"--path-format=absolute"},
                                              StringLiteral{"--git-path"},
                                              StringLiteral{"index"}};
        return run_git_cmd(context, git_exe, locator, args).maybe_output.map([](std::string&& proto_path) -> Path {
            return std::move(proto_path);
        });
    }

    bool git_add_with_index(DiagnosticContext& context, const Path& git_exe, const Path& target, const Path& index_file)
    {
        static constexpr StringView args[] = {StringLiteral{"add"}, StringLiteral{"-A"}, StringLiteral{"."}};
        return run_git_cmd_with_index(
                   context, git_exe, GitRepoLocator{GitRepoLocatorKind::CurrentDirectory, target}, index_file, args)
            .maybe_output.has_value();
    }

    Optional<std::string> git_write_index_tree(DiagnosticContext& context,
                                               const Path& git_exe,
                                               GitRepoLocator locator,
                                               const Path& index_file)
    {
        static constexpr StringView args[] = {StringLiteral{"write-tree"}};
        return run_git_cmd_with_index(context, git_exe, locator, index_file, args).maybe_output;
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
        auto maybe_ls_tree_result = run_git_cmd(context, git_exe, locator, args, launch_settings);
        if (auto ls_tree_output = maybe_ls_tree_result.maybe_output.get())
        {
            if (parse_git_ls_tree_output(
                    context, result.emplace(), *ls_tree_output, maybe_ls_tree_result.command.command_line()))
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
        auto maybe_git_read_tree_output =
            run_git_cmd_with_index(context, git_exe, locator, git_tree_index, read_tree_args);
        if (maybe_git_read_tree_output.maybe_output.has_value())
        {
            auto prefix_arg = fmt::format("--prefix={}/", git_tree_temp);
            StringView checkout_index_args[] = {StringLiteral{"--work-tree"},
                                                git_tree_temp,
                                                StringLiteral{"checkout-index"},
                                                StringLiteral{"-af"},
                                                StringLiteral{"--ignore-skip-worktree-bits"},
                                                prefix_arg};
            auto git_checkout_index_command =
                run_git_cmd_with_index(context, git_exe, locator, git_tree_index, checkout_index_args);
            bool succeeded = git_checkout_index_command.maybe_output.has_value();
            fs.remove(git_tree_index, IgnoreErrors{});
            return succeeded && fs.rename_or_delete(context, git_tree_temp, destination).has_value();
        }

        if (is_shallow_clone(context, git_exe, locator).value_or(false))
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
        static constexpr StringLiteral VALID_COMMIT_OUTPUT = "commit\n";
        StringView args[] = {StringLiteral{"cat-file"}, StringLiteral{"-t"}, git_commit_id};
        auto maybe_cat_file_output = run_git_cmd(context, git_exe, locator, args);
        if (auto output = maybe_cat_file_output.maybe_output.get())
        {
            return *output == VALID_COMMIT_OUTPUT;
        }

        return nullopt;
    }

    Optional<std::string> git_merge_base(
        DiagnosticContext& context, const Path& git_exe, GitRepoLocator locator, StringView commit1, StringView commit2)
    {
        StringView args[] = {StringLiteral{"merge-base"}, commit1, commit2};
        auto maybe_merge_base_output = run_git_cmd(context, git_exe, locator, args);
        if (auto output = maybe_merge_base_output.maybe_output.get())
        {
            Strings::inplace_trim_end(*output);
            if (is_git_sha(*output))
            {
                return std::move(*output);
            }

            context.report_error_with_log(*output,
                                          msgGitUnexpectedCommandOutputCmd,
                                          msg::command_line = maybe_merge_base_output.command.command_line());
        }

        return nullopt;
    }

    bool parse_git_diff_tree_line(std::vector<GitDiffTreeLine>& target, const char*& original_first, const char* last)
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

        auto first = original_first;
        first = std::find(first, last, ':');
        static constexpr ptrdiff_t minimum_prefix_size = 1 + 7 + 7 + 41 + 41 + 2;
        if ((last - first) < minimum_prefix_size || first[0] != ':' || first[7] != ' ' || first[14] != ' ' ||
            first[55] != ' ' || first[96] != ' ')
        {
            return false;
        }

        ++first; // skip :
        StringView old_mode{first, 6};
        if (!is_git_mode(old_mode))
        {
            return false;
        }

        first += 7; // +1 to skip space, ditto below
        StringView new_mode{first, 6};
        if (!is_git_mode(new_mode))
        {
            return false;
        }

        first += 7;
        StringView old_sha{first, 40};
        if (!is_git_sha(old_sha))
        {
            return false;
        }

        first += 41;
        StringView new_sha{first, 40};
        if (!is_git_sha(new_sha))
        {
            return false;
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
            default: return false;
        }

        int score;
        ++first;
        static constexpr auto is_tab_or_nul = [](char ch) noexcept { return ch == '\0' || ch == '\t'; };
        if (first != last && !is_tab_or_nul(*first))
        {
            auto score_end = std::find_if(first, last, is_tab_or_nul);
            if (score_end == last)
            {
                return false;
            }

            auto maybe_score = Strings::strto<int>(StringView{first, score_end});
            if (auto p_score = maybe_score.get())
            {
                score = *p_score;
            }
            else
            {
                return false;
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
                return false;
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
            return false;
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
        original_first = first;
        return true;
    }

    Optional<std::vector<GitDiffTreeLine>> parse_git_diff_tree_lines(DiagnosticContext& context,
                                                                     StringView command_line,
                                                                     StringView output)
    {
        Optional<std::vector<GitDiffTreeLine>> result_storage;
        auto& result = result_storage.emplace();
        const char* first = output.begin();
        const char* const last = output.end();
        while (first != last)
        {
            if (!parse_git_diff_tree_line(result, first, last))
            {
                context.report_error_with_log(
                    output, msgGitUnexpectedCommandOutputCmd, msg::command_line = command_line);
                result_storage.clear();
                return result_storage;
            }
        }

        return result_storage;
    }

    Optional<std::vector<GitDiffTreeLine>> git_diff_tree(
        DiagnosticContext& context, const Path& git_exe, GitRepoLocator locator, StringView tree1, StringView tree2)
    {
        RedirectedProcessLaunchSettings launch_settings;
        launch_settings.encoding = Encoding::Utf8WithNulls;
        StringView args[] = {StringLiteral{"diff-tree"}, StringLiteral{"-z"}, tree1, tree2};
        auto maybe_git_diff_tree_output = run_git_cmd(context, git_exe, locator, args, launch_settings);
        if (auto git_diff_tree_output = maybe_git_diff_tree_output.maybe_output.get())
        {
            Strings::inplace_trim_end(*git_diff_tree_output);
            return parse_git_diff_tree_lines(
                context, maybe_git_diff_tree_output.command.command_line(), *git_diff_tree_output);
        }
        return nullopt;
    }
}
