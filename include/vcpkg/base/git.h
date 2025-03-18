#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/git.h>

#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/stringview.h>

#include <set>
#include <string>
#include <vector>

namespace vcpkg
{
    struct GitConfig
    {
        Path git_exe;
        Path git_dir;
        Path git_work_tree;
    };

    struct GitStatusLine
    {
        enum class Status
        {
            Unmodified,
            Modified,
            TypeChanged,
            Added,
            Deleted,
            Renamed,
            Copied,
            Unmerged,
            Untracked,
            Ignored,
            Unknown
        };

        Status index_status = Status::Unknown;
        Status work_tree_status = Status::Unknown;
        std::string path;
        std::string old_path;
    };

    struct GitLSTreeEntry
    {
        std::string file_name;
        std::string git_tree_sha;
    };

    // Attempts to parse the git status output returns a parsing error message on failure
    ExpectedL<std::vector<GitStatusLine>> parse_git_status_output(StringView git_status_output,
                                                                  StringView git_command_line);

    // Run git status on a repository, optionaly a specific subpath can be queried
    ExpectedL<std::vector<GitStatusLine>> git_status(const GitConfig& config, StringView path = "");

    // Check whether a repository is a shallow clone
    ExpectedL<bool> is_shallow_clone(const GitConfig& config);

    Optional<std::string> temp_index_file_path_for_directory(DiagnosticContext& context,
                                                             const Path& git_exe,
                                                             const Path& target);

    Optional<std::string> write_git_tree(DiagnosticContext& context,
                                         const Filesystem& fs,
                                         const Path& git_exe,
                                         const Path& temp_index_file,
                                         const Path& target);

    Optional<std::vector<GitLSTreeEntry>> ls_tree(DiagnosticContext& context,
                                                  const Path& git_exe,
                                                  const Path& working_directory,
                                                  StringView treeish);
}
