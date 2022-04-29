#pragma once

#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>

#include <set>
#include <vector>
#include <unordered_map>

namespace vcpkg
{
    namespace Git
    {
        enum class DirsOnly
        {
            NO = 0,
            YES
        };

        enum class Recursive
        {
            NO = 0,
            YES
        };
    }

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

    struct GitLsTreeLine
    {
        std::string mode;
        std::string type;
        std::string git_object;
        std::string path;
    };

    /* ===== Git command abstractions  =====*/
    // run git status on a repository, optionaly a specific subpath can be queried
    ExpectedL<std::vector<GitStatusLine>> git_status(const GitConfig& config, StringView path = {});

    // initializes a git repository
    ExpectedL<bool> git_init(const GitConfig& config);

    // fetch a repository into the specified work tree
    // the directory pointed at by config.work_tree should already exist
    ExpectedL<bool> git_fetch(const GitConfig& config, StringView uri, StringView ref);

    // returns the current commit of the specified ref (HEAD by default)
    ExpectedL<std::string> git_rev_parse(const GitConfig& config, StringView ref, StringView path = {});

    // lists the contents of a tree object
    ExpectedL<std::vector<GitLsTreeLine>> git_ls_tree(const GitConfig& config,
                                                      StringView ref,
                                                      StringView path = {},
                                                      Git::Recursive recursive = Git::Recursive::NO,
                                                      Git::DirsOnly dirs_only = Git::DirsOnly::NO);

    // runs `git show {git_object}`, optionally a path inside the object can be given
    ExpectedL<std::string> git_show(const GitConfig& config, StringView git_object, StringView path = {});

    /* ===== Git application business logic =====*/
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
    ExpectedL<Path> git_checkout_port(const GitConfig& config,
                                      Filesystem& fs,
                                      const Path& cmake_exe,
                                      const Path& containing_dir,
                                      StringView port_name,
                                      StringView git_object);

    // checks out a registry port into containing dir
    ExpectedL<Path> git_checkout_registry_port(const GitConfig& config,
                                               Filesystem& fs,
                                               const Path& cmake_exe,
                                               const Path& containing_dir,
                                               StringView git_object);

    ExpectedL<std::unordered_map<std::string, std::string>> git_ports_tree_map(const GitConfig& config, StringView ref);

    /* ==== Testable helpers =====*/
    // Try to extract a port name from a path.
    // The path should start with the "ports/" prefix
    std::string try_extract_port_name_from_path(StringView path);

    // attempts to parse git status output
    ExpectedL<std::vector<GitStatusLine>> parse_git_status_output(StringView git_status_output);

    // attempts to parse git ls-tree output
    ExpectedL<std::vector<GitLsTreeLine>> parse_git_ls_tree_output(StringView git_ls_tree_output);
}
