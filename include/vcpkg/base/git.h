#pragma once

#include <vcpkg/base/fwd/git.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.process.h>

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

    Command git_cmd_builder(const GitConfig& config);

    // Try to extract a port name from a path.
    // The path should start with the "ports/" prefix
    std::string try_extract_port_name_from_path(StringView path);

    // Attempts to parse the git status output returns a parsing error message on failure
    ExpectedL<std::vector<GitStatusLine>> parse_git_status_output(StringView git_status_output,
                                                                  StringView git_command_line);

    // Run git status on a repository, optionaly a specific subpath can be queried
    ExpectedL<std::vector<GitStatusLine>> git_status(const GitConfig& config, StringView path = "");

    // Returns a list of ports that have uncommitted/unmerged changes
    ExpectedL<std::set<std::string>> git_ports_with_uncommitted_changes(const GitConfig& config);

    // Check whether a repository is a shallow clone
    ExpectedL<bool> is_shallow_clone(const GitConfig& config);

    // runs git ref-parse for a given refname, e.g. HEAD
    ExpectedL<std::string> git_ref_sha(const GitConfig& config, StringView refname = "HEAD");
    // runs `git fetch {uri} {treeish}`
    ExpectedL<Unit> git_fetch(const Filesystem& fs, const GitConfig& config, StringView repo, StringView treeish);
}
