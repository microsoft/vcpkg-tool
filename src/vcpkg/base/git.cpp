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

    GitCmdResult run_git_cmd_impl(vcpkg::DiagnosticContext& context,
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
        if (auto prefix_output = check_zero_exit_code(context, maybe_result, git_exe))
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
        return run_git_cmd_impl(context, git_exe, locator, additional_args, launch_settings);
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
        return run_git_cmd_impl(context, git_exe, locator, additional_args, launch_settings);
    }
}

namespace vcpkg
{
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

    Optional<std::string> git_index_file(DiagnosticContext& context, const Path& git_exe, GitRepoLocator locator)
    {
        static constexpr StringView args[] = {StringLiteral{"rev-parse"},
                                              StringLiteral{"--path-format=absolute"},
                                              StringLiteral{"--git-path"},
                                              StringLiteral{"index"}};
        return run_git_cmd(context, git_exe, locator, args).maybe_output;
    }

    Optional<std::string> git_git_dir(DiagnosticContext& context, const Path& git_exe, const Path& target)
    {
        static constexpr StringView args[] = {
            StringLiteral{"rev-parse"}, StringLiteral{"--path-format=absolute"}, StringLiteral{"--git-dir"}};
        return run_git_cmd(context, git_exe, GitRepoLocator{GitRepoLocatorKind::CurrentDirectory, target}, args)
            .maybe_output;
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

    Optional<std::vector<GitLSTreeEntry>> git_ls_tree(DiagnosticContext& context,
                                                      const Path& git_exe,
                                                      GitRepoLocator locator,
                                                      StringView treeish)
    {
        Optional<std::vector<GitLSTreeEntry>> result;
        auto& ret = result.emplace();
        StringView args[] = {StringLiteral{"ls-tree"}, treeish, StringLiteral{"--full-tree"}};
        auto maybe_ls_tree_result = run_git_cmd(context, git_exe, locator, args);
        if (auto ls_tree_output = maybe_ls_tree_result.maybe_output.get())
        {
            const auto lines = Strings::split(*ls_tree_output, '\n');
            // The first line of the output is always the parent directory itself.
            for (auto&& line : lines)
            {
                // The default output comes in the format:
                // <mode> SP <type> SP <object> TAB <file>
                auto split_line = Strings::split(line, '\t');
                if (split_line.size() != 2)
                {
                    context.report_error_with_log(*ls_tree_output,
                                                  msgGitUnexpectedCommandOutputCmd,
                                                  msg::command_line = maybe_ls_tree_result.command.command_line());
                    result.clear();
                    return result;
                }

                auto file_info_section = Strings::split(split_line[0], ' ');
                if (file_info_section.size() != 3)
                {
                    context.report_error_with_log(*ls_tree_output,
                                                  msgGitUnexpectedCommandOutputCmd,
                                                  msg::command_line = maybe_ls_tree_result.command.command_line());
                    result.clear();
                    return result;
                }

                ret.push_back(GitLSTreeEntry{split_line[1], file_info_section.back()});
            }

            return result;
        }

        result.clear();
        return result;
    }

    bool git_read_tree(DiagnosticContext& context,
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
        if (auto git_read_tree_output = maybe_git_read_tree_output.maybe_output.get())
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
                return *output;
            }

            context.report_error_with_log(*output,
                                          msgGitUnexpectedCommandOutputCmd,
                                          msg::command_line = maybe_merge_base_output.command.command_line());
        }

        return nullopt;
    }
}
