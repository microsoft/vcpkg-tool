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

        /// \param rev Uses git revision syntax (e.g. <commit>[:<subpath>])
        virtual ExpectedL<std::string> show(const GitConfig& repo, StringView rev) const = 0;

        /// \param path An optional subpath to be queried
        virtual ExpectedL<std::vector<GitStatusLine>> status(const GitConfig& config, StringView path = {}) const = 0;

        virtual ExpectedL<std::vector<GitLsTreeLine>> ls_tree(const GitConfig& config,
                                                              StringView rev,
                                                              GitLsTreeOptions options = {}) const = 0;

        /// Equivalent to 'git log "--format=%H %cd" --date=short --left-only -- <path>'
        virtual ExpectedL<std::vector<GitLogResult>> log(const GitConfig& config, StringView path) const = 0;

        /// \param rev Commit to checkout files from
        /// \param files Required list of files to checkout
        virtual ExpectedL<char> checkout(const GitConfig& config, StringView rev, View<StringView> files) const = 0;

        virtual ExpectedL<char> reset(const GitConfig& config) const = 0;

        /// Determine if \c rev is in the git repo and points at a commit object
        /// \returns The boolean value of  "is \c rev a commit object" on success.
        virtual ExpectedL<bool> is_commit(const GitConfig& config, StringView rev) const = 0;

        /// Atomically creates destination with contents of git tree object. If destination exists, assumes it has
        /// correct contents
        /// \param cmake_exe CMake executable to use for unpacking intermediate archive files
        /// \param destination Directory to create with contents of tree
        /// \param rev Uses git revision syntax (e.g. <commit>[:<subpath>] or <sha>)
        /// \returns \c destination on success
        virtual ExpectedL<Path> splat_object(const GitConfig& config,
                                             Filesystem& fs,
                                             const Path& cmake_exe,
                                             const Path& destination,
                                             StringView rev) const = 0;

        /// Runs 'git init && git fetch {uri} {rev}:<temporary>'.
        /// \param uri URI to fetch.
        /// \param rev Revision to fetch. Set to HEAD for the default branch.
        /// \returns \c rev_parse of the fetched revision on success.
        virtual ExpectedL<std::string> init_fetch(const GitConfig& config,
                                                  Filesystem& fs,
                                                  StringView uri,
                                                  StringView rev) const = 0;
    };

    std::unique_ptr<IGit> make_git_from_exe(StringView git_exe);

    /* ==== Testable helpers =====*/
    // Try to extract a port name from a path.
    // The path should start with the "ports/" prefix
    std::string try_extract_port_name_from_path(StringView path);

    // attempts to parse git status output
    ExpectedL<std::vector<GitStatusLine>> parse_git_status_output(StringView git_status_output);

    // attempts to parse git ls-tree output
    ExpectedL<std::vector<GitLsTreeLine>> parse_git_ls_tree_output(StringView git_ls_tree_output);
}
