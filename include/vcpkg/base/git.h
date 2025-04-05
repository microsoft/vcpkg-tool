#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/git.h>

#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/stringview.h>

#include <set>
#include <string>
#include <vector>

namespace vcpkg
{
    struct GitRepoLocator
    {
        GitRepoLocatorKind kind;
        const Path& path;
    };

    struct GitLSTreeEntry
    {
        std::string file_name;
        std::string git_tree_sha;

        GitLSTreeEntry(StringLiteral file_name, StringLiteral git_tree_sha);
        explicit GitLSTreeEntry(std::string&& file_name, std::string&& git_tree_sha);

        friend bool operator==(const GitLSTreeEntry& lhs, const GitLSTreeEntry& rhs) noexcept;
        friend bool operator!=(const GitLSTreeEntry& lhs, const GitLSTreeEntry& rhs) noexcept;
    };

    // https://git-scm.com/docs/git-diff-tree#_raw_output_format
    struct GitDiffTreeLine
    {
        std::string old_mode;
        std::string new_mode;
        std::string old_sha;
        std::string new_sha;
        GitDiffTreeLineKind kind;
        int score;
        std::string file_name;
        std::string old_file_name;

        friend bool operator==(const GitDiffTreeLine& lhs, const GitDiffTreeLine& rhs) noexcept;
        friend bool operator!=(const GitDiffTreeLine& lhs, const GitDiffTreeLine& rhs) noexcept;
    };

    bool is_git_mode(StringView sv) noexcept;

    bool is_git_sha(StringView sv) noexcept;

    Optional<bool> is_shallow_clone(DiagnosticContext& context, const Path& git_exe, GitRepoLocator locator);

    Optional<std::string> git_prefix(DiagnosticContext& context, const Path& git_exe, const Path& target);

    Optional<Path> git_index_file(DiagnosticContext& context,
                                  const Filesystem& fs,
                                  const Path& git_exe,
                                  GitRepoLocator locator);

    Optional<Path> git_absolute_git_dir(DiagnosticContext& context,
                                        const Filesystem& fs,
                                        const Path& git_exe,
                                        GitRepoLocator locator);

    bool git_add_with_index(DiagnosticContext& context,
                            const Path& git_exe,
                            const Path& target,
                            const Path& index_file);

    Optional<std::string> git_write_index_tree(DiagnosticContext& context,
                                               const Path& git_exe,
                                               GitRepoLocator locator,
                                               const Path& index_file);

    bool parse_git_ls_tree_output(DiagnosticContext& context,
                                  std::vector<GitLSTreeEntry>& target,
                                  StringView ls_tree_output,
                                  StringView ls_tree_command);

    Optional<std::vector<GitLSTreeEntry>> git_ls_tree(DiagnosticContext& context,
                                                      const Path& git_exe,
                                                      GitRepoLocator locator,
                                                      StringView treeish);

    bool git_extract_tree(DiagnosticContext& context,
                          const Filesystem& fs,
                          const Path& git_exe,
                          GitRepoLocator locator,
                          const Path& destination,
                          StringView treeish);

    Optional<bool> git_check_is_commit(DiagnosticContext& context,
                                       const Path& git_exe,
                                       GitRepoLocator locator,
                                       StringView git_commit_id);

    Optional<std::string> git_merge_base(DiagnosticContext& context,
                                         const Path& git_exe,
                                         GitRepoLocator locator,
                                         StringView commit1,
                                         StringView commit2);

    const char* parse_git_diff_tree_line(std::vector<GitDiffTreeLine>& target, const char* first, const char* last);

    Optional<std::vector<GitDiffTreeLine>> parse_git_diff_tree_lines(DiagnosticContext& context,
                                                                     StringView command_line,
                                                                     StringView output);

    Optional<std::vector<GitDiffTreeLine>> git_diff_tree(
        DiagnosticContext& context, const Path& git_exe, GitRepoLocator locator, StringView tree1, StringView tree2);
}
