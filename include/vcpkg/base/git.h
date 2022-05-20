#pragma once

#include <vcpkg/base/fwd/git.h>
#include <vcpkg/base/fwd/span.h>
#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>

#include <set>
#include <unordered_map>
#include <vector>

namespace vcpkg
{
    struct GitConfig
    {
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

    /* ==== Testable helpers =====*/
    // Try to extract a port name from a path.
    // The path should start with the "ports/" prefix
    std::string try_extract_port_name_from_path(StringView path);

    // attempts to parse git status output
    ExpectedL<std::vector<GitStatusLine>> parse_git_status_output(StringView git_status_output);

    // attempts to parse git ls-tree output
    ExpectedL<std::vector<GitLsTreeLine>> parse_git_ls_tree_output(StringView git_ls_tree_output);

    struct GitLsTreeOptions
    {
        StringView path;
        bool recursive;
        bool dirs_only;
    };

    struct GitLogResult
    {
        std::string commit;
        std::string date;
    };

    /// Abstracts different git implementations, such as a user-provided git binary, libgit2, or a fake/mock.
    struct IGit
    {
        /// Outputs as though "--pretty=format:%h %cs (%cr)" was specified
        /// \param rev Uses git revision syntax (e.g. <commit>[:<subpath>])
        virtual ExpectedL<std::string> show_pretty_commit(const GitConfig& repo, StringView rev) const = 0;

        /// \param rev Uses git revision syntax (e.g. <commit>[:<subpath>])
        virtual ExpectedL<std::string> rev_parse(const GitConfig& config, StringView rev) const = 0;

        /// If destination exists, immediately returns.
        /// \param rev Uses git revision syntax (e.g. <commit>[:<subpath>])
        virtual ExpectedL<char> archive(const GitConfig& config, StringView rev, StringView destination) const = 0;

        /// \param rev Uses git revision syntax (e.g. <commit>[:<subpath>])
        virtual ExpectedL<std::string> show(const GitConfig& repo, StringView rev) const = 0;

        /// \param path An optional subpath to be queried
        virtual ExpectedL<std::vector<GitStatusLine>> status(const GitConfig& config, StringView path = {}) const = 0;

        virtual ExpectedL<std::vector<GitLsTreeLine>> ls_tree(const GitConfig& config,
                                                              StringView rev,
                                                              GitLsTreeOptions options = {}) const = 0;

        /// log "--format=%H %cd" --date=short --left-only -- <path>
        virtual ExpectedL<std::vector<GitLogResult>> log(const GitConfig& config, StringView path) const = 0;

        virtual ExpectedL<char> checkout(const GitConfig& config, StringView rev, View<StringView> files) const = 0;

        virtual ExpectedL<char> reset(const GitConfig& config) const = 0;

        virtual ExpectedL<bool> is_commit(const GitConfig& config, StringView rev) const = 0;

        // initializes a git repository
        virtual ExpectedL<bool> init(const GitConfig& config) const = 0;

        // fetch a repository into the specified work tree
        // the directory pointed at by config.work_tree should already exist
        virtual ExpectedL<bool> fetch(const GitConfig& config, StringView uri, StringView ref) const = 0;

        /* ===== Git application business logic =====*/
        // runs 'git fetch {url} {treeish}' and returns the hash of FETCH_HEAD
        // set {treeish} to HEAD for the default branch
        virtual ExpectedL<std::string> git_fetch_from_remote_registry(const GitConfig& config,
                                                                      Filesystem& fs,
                                                                      StringView uri,
                                                                      StringView ref) const = 0;
        // returns the current git commit SHA
        virtual ExpectedL<std::string> git_current_sha(const GitConfig& config,
                                                       Optional<std::string> maybe_embedded_sha = nullopt) const = 0;

        virtual LocalizedString git_current_sha_message(const GitConfig& config,
                                                        Optional<std::string> maybe_embedded_sha = nullopt) const = 0;

        // checks out a port version into containing_dir
        virtual ExpectedL<Path> git_checkout_port(const GitConfig& config,
                                                  Filesystem& fs,
                                                  const Path& cmake_exe,
                                                  const Path& containing_dir,
                                                  StringView port_name,
                                                  StringView git_object) const = 0;

        // checks out a registry port into containing dir
        virtual ExpectedL<Path> git_checkout_registry_port(const GitConfig& config,
                                                           Filesystem& fs,
                                                           const Path& cmake_exe,
                                                           const Path& containing_dir,
                                                           StringView git_object) const = 0;

        virtual ExpectedL<std::unordered_map<std::string, std::string>> git_ports_tree_map(const GitConfig& config,
                                                                                           StringView ref) const = 0;
    };

    std::unique_ptr<IGit> make_git_from_exe(StringView git_exe);
}
