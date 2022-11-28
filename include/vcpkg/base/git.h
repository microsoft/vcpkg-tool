#pragma once

#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>

#include <set>
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

    // Try to extract a port name from a path.
    // The path should start with the "ports/" prefix
    std::string try_extract_port_name_from_path(StringView path);

    // Attempts to parse the git status output returns a parsing error message on failure
    ExpectedL<std::vector<GitStatusLine>> parse_git_status_output(StringView git_status_output);

    // Run git status on a repository, optionaly a specific subpath can be queried
    ExpectedL<std::vector<GitStatusLine>> git_status(const GitConfig& config, StringView path = "");

    // Returns a list of ports that have uncommitted/unmerged changes
    ExpectedL<std::set<std::string>> git_ports_with_uncommitted_changes(const GitConfig& config);

    // Check whether a repository is a shallow clone
    ExpectedL<bool> is_shallow_clone(const GitConfig& config);
}
