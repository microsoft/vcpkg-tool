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

    /* ===== Git command abstractions  =====*/
    // run git status on a repository, optionaly a specific subpath can be queried
    ExpectedL<std::vector<GitStatusLine>> git_status(const GitConfig& config, StringView path = "");

    // fetch a repository into the specified work tree
    // the directory pointed at by config.work_tree should already exist
    ExpectedL<bool> git_fetch(const GitConfig& config, StringView uri, StringView ref);

    // returns the current commit of the specified ref (HEAD by default)
    ExpectedL<std::string> git_rev_parse(const GitConfig& config, StringView ref = "HEAD");

    // runs `git show {git_object}`, optionally a path inside the object can be given
    ExpectedL<std::string> git_show(const GitConfig& config, StringView git_object, StringView path = "");

    /* ===== Git business application layer =====*/
    // returns a list of ports that have uncommitted/unmerged changes
    ExpectedL<std::set<std::string>> git_ports_with_uncommitted_changes(const GitConfig& config);

    // runs 'git fetch {url} {treeish}' and returns the hash of FETCH_HEAD
    // set {treeish} to HEAD for the default branch
    ExpectedL<std::string> git_fetch_from_remote_registry(const GitConfig& config,
                                                          Filesystem& fs,
                                                          StringView uri,
                                                          StringView ref);
    // returns the current git commit SHA
    ExpectedL<std::string> git_current_sha(const GitConfig& config, Optional<std::string> maybe_embedded_sha = nullopt);

    // checks out a port version into containing_dir
    // returns the path to the checked out port
    ExpectedL<Path> git_checkout_port(const GitConfig& config,
                                      Filesystem& fs,
                                      const Path& cmake_exe,
                                      const Path& containing_dir,
                                      StringView port_name,
                                      StringView git_object);

    /* ==== Testable helpers =====*/
    // Try to extract a port name from a path.
    // The path should start with the "ports/" prefix
    std::string try_extract_port_name_from_path(StringView path);

    // Attempts to parse the git status output returns a parsing error message on failure
    ExpectedL<std::vector<GitStatusLine>> parse_git_status_output(StringView git_status_output);
}
