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

    Command git_cmd_builder(const Path& git_exe, GitRepoLocator locator, View<StringView> additional_args)
    {
        static constexpr StringLiteral DashC{"-C"};
        static constexpr StringLiteral GitDir{"--git-dir"};
        Command cmd(git_exe);
        const StringLiteral* arg_name;
        switch (locator.kind)
        {
            case GitRepoLocatorKind::CurrentDirectory: arg_name = &DashC; break;
            case GitRepoLocatorKind::DotGitDir: arg_name = &GitDir; break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
        cmd.string_arg(*arg_name);
        cmd.string_arg(locator.path);
        cmd.string_arg("-c").string_arg("core.autocrlf=false");
        for (auto&& additional_arg : additional_args)
        {
            cmd.string_arg(additional_arg);
        }

        return cmd;
    }

    Optional<std::string> run_git_cmd(DiagnosticContext& context,
                                      const Path& git_exe,
                                      GitRepoLocator locator,
                                      View<StringView> additional_args)
    {
        Command cmd = git_cmd_builder(git_exe, locator, additional_args);
        auto maybe_result = cmd_execute_and_capture_output(context, cmd);
        if (auto prefix_output = check_zero_exit_code(context, maybe_result, git_exe))
        {
            Strings::inplace_trim_end(*prefix_output);
            return std::move(*prefix_output);
        }

        return nullopt;
    }

    Optional<std::string> run_git_cmd_with_index(DiagnosticContext& context,
                                                 const Path& git_exe,
                                                 GitRepoLocator locator,
                                                 const Path& index_file,
                                                 View<StringView> additional_args)
    {
        Command cmd = git_cmd_builder(git_exe, locator, additional_args);
        RedirectedProcessLaunchSettings launch_settings;
        auto& environment = launch_settings.environment.emplace();
        environment.add_entry("GIT_INDEX_FILE", index_file);

        auto maybe_result = cmd_execute_and_capture_output(context, cmd, launch_settings);
        if (auto prefix_output = check_zero_exit_code(context, maybe_result, git_exe))
        {
            Strings::inplace_trim_end(*prefix_output);
            return std::move(*prefix_output);
        }

        return nullopt;
    }
}

namespace vcpkg
{
    Optional<bool> is_shallow_clone(DiagnosticContext& context, const Path& git_exe, GitRepoLocator locator)
    {
        static constexpr StringView args[] = {StringLiteral{"rev-parse"}, StringLiteral{"--is-shallow-repository"}};
        return run_git_cmd(context, git_exe, locator, args).map([](std::string&& output) {
            return "true" == Strings::trim(std::move(output));
        });
    }

    Optional<std::string> git_prefix(DiagnosticContext& context, const Path& git_exe, const Path& target)
    {
        static constexpr StringView args[] = {StringLiteral{"rev-parse"}, StringLiteral{"--show-prefix"}};
        return run_git_cmd(context, git_exe, GitRepoLocator{GitRepoLocatorKind::CurrentDirectory, target}, args);
    }

    Optional<std::string> git_index_file(DiagnosticContext& context, const Path& git_exe, GitRepoLocator locator)
    {
        static constexpr StringView args[] = {StringLiteral{"rev-parse"},
                                              StringLiteral{"--path-format=absolute"},
                                              StringLiteral{"--git-path"},
                                              StringLiteral{"index"}};
        return run_git_cmd(context, git_exe, locator, args);
    }

    Optional<std::string> git_git_dir(DiagnosticContext& context, const Path& git_exe, const Path& target)
    {
        static constexpr StringView args[] = {
            StringLiteral{"rev-parse"}, StringLiteral{"--path-format=absolute"}, StringLiteral{"--git-dir"}};
        return run_git_cmd(context, git_exe, GitRepoLocator{GitRepoLocatorKind::CurrentDirectory, target}, args);
    }

    bool git_add_with_index(DiagnosticContext& context, const Path& git_exe, const Path& target, const Path& index_file)
    {
        static constexpr StringView args[] = {StringLiteral{"add"}, StringLiteral{"-A"}, StringLiteral{"."}};
        return run_git_cmd_with_index(
                   context, git_exe, GitRepoLocator{GitRepoLocatorKind::CurrentDirectory, target}, index_file, args)
            .has_value();
    }

    Optional<std::string> git_write_index_tree(DiagnosticContext& context,
                                               const Path& git_exe,
                                               GitRepoLocator locator,
                                               const Path& index_file)
    {
        static constexpr StringView args[] = {StringLiteral{"write-tree"}};
        return run_git_cmd_with_index(context, git_exe, locator, index_file, args);
    }

    Optional<std::vector<GitLSTreeEntry>> git_ls_tree(DiagnosticContext& context,
                                                      const Path& git_exe,
                                                      GitRepoLocator locator,
                                                      StringView treeish)
    {
        StringView args[] = {StringLiteral{"ls-tree"}, treeish, StringLiteral{"--full-tree"}};
        Command cmd = git_cmd_builder(git_exe, locator, args);
        auto maybe_ls_tree_result = cmd_execute_and_capture_output(context, cmd);
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

        RedirectedProcessLaunchSettings index_settings;
        auto& env = index_settings.environment.emplace();
        env.add_entry("GIT_INDEX_FILE", git_tree_index.native());

        StringView read_tree_args[] = {StringLiteral{"read-tree"}, treeish};
        auto read_tree_command = git_cmd_builder(git_exe, locator, read_tree_args);
        auto maybe_git_read_tree_output = cmd_execute_and_capture_output(context, read_tree_command, index_settings);
        if (auto git_read_tree_output = check_zero_exit_code(context, maybe_git_read_tree_output, git_exe))
        {
            auto prefix_arg = fmt::format("--prefix={}/", git_tree_temp);
            StringView args2[] = {StringLiteral{"--work-tree"},
                                  git_tree_temp,
                                  StringLiteral{"checkout-index"},
                                  StringLiteral{"-af"},
                                  StringLiteral{"--ignore-skip-worktree-bits"},
                                  prefix_arg};
            auto git_checkout_index_command = git_cmd_builder(git_exe, locator, args2);
            auto maybe_git_checkout_index_output =
                cmd_execute_and_capture_output(context, git_checkout_index_command, index_settings);
            bool succeeded = check_zero_exit_code(context, maybe_git_checkout_index_output, git_exe) != nullptr;
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
        if (auto output = maybe_cat_file_output.get())
        {
            return *output == VALID_COMMIT_OUTPUT;
        }

        return nullopt;
    }
}
