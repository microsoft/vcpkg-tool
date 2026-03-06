#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/binaryparagraph.h>

#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/path.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/triplet.h>

namespace vcpkg
{
    struct InstalledPaths
    {
        explicit InstalledPaths(Path&& root) : m_root(std::move(root)) { }

        // A vcpkg "installed" tree consists of installed contents and a database that describes what is installed.
        //
        // The paths below assume root() is / (that is, typically vcpkg_dir/installed or vcpkg.json_dir/vcpkg_installed)
        const Path& root() const { return m_root; }

        // Creates the minimum directory structure for an installed tree.
        void create_directories(const Filesystem& fs) const;

        Path vcpkg_dir() const { return m_root / FileVcpkg; }

        // /vcpkg/compiler-file-hash-cache.json, caches the SHA of detected compilers as that is expensive on platforms
        // where the compiler binary is often ~60MB or more
        Path compiler_hash_cache_file() const { return vcpkg_dir() / FileCompilerFileHashCacheDotJson; }
        // /vcpkg/issue_body.md, where the body of a GitHub issue is written on install failure to make it convenient
        // for users to pass to `gh`
        Path issue_body_path() const { return vcpkg_dir() / FileIssueBodyMD; }
        // vcpkg/vcpkg-lock.json stores information about data fetched in resolving registry references
        // (this is the 'npm lockfile' meaning of lockfile, not a file system lock)
        Path lockfile_path() const { return vcpkg_dir() / FileVcpkgLock; }
        // vcpkg/manifest-info.json, stores the path to the manifest file from which an installed tree was generated;
        // this helps tools like component governance that want to report errors about dependencies detected in this
        // installed tree be reported in the file that led to them being installed.
        Path manifest_info_path() const { return vcpkg_dir() / FileManifestInfo; }

        // /vcpkg/info/<name>_<version_<triplet>.list, contains a list of every file installed by a given package
        // These list files are created by install_files_and_write_listfile
        Path vcpkg_dir_info() const { return vcpkg_dir() / FileInfo; }
        Path listfile_path(const BinaryParagraph& pgh) const;

        // /vcpkg/status contains information about which packages are installed in this tree
        Path vcpkg_dir_status_file() const { return vcpkg_dir() / FileStatus; }
        // /vcpkg/updates/*
        // rolling updates to the status file, written by database_write_update.
        Path vcpkg_dir_updates() const { return vcpkg_dir() / FileUpdates; }
        // database_load will read status and the updates to have information about what this installed tree currently
        // contains database_sync will also collapse all the update files into the status file, and delete the
        // update files. This avoids the status database being corrupted if vcpkg is terminated while it is running
        // (each update is written immediately after each port install and the results merged only at the end)

        // /vcpkg/vcpkg-running.lock is a file system lock used to synchronize access to the installed tree
        Path vcpkg_running_lock() const { return vcpkg_dir() / FileVcpkgRunningLock; }

        // /<triplet> is the root for user facing contents associated with <triplet>
        // for example, /x64-windows/include/zlib.h would be the path to zlib's public header when installed for
        // x64-windows
        Path triplet_dir(Triplet t) const { return m_root / t.canonical_name(); }
        // /<triplet>/<share>/<name> typically stores build system bindings for <name> and some communication to
        // vcpkg, for example:
        // /<triplet>/share/<name>/copyright stores the license information for <name>
        // /<triplet>/share/<name>/usage provides for customized usage
        // /<triplet>/share/<name>/vcpkg_abi_info.txt is the file hashed to form a package ABI
        // /<triplet>/share/<name>/vcpkg.spdx.json stores information about what should be included in an SBOM for
        // that package
        // /<triplet>/share/<name>/vcpkg-port-config.cmake is included automatically in ports that directly depend on
        // <name>
        Path share_dir(const PackageSpec& p) const { return triplet_dir(p.triplet()) / FileShare / p.name(); }
        Path usage_file(const PackageSpec& p) const { return share_dir(p) / FileUsage; }
        Path spdx_file(const PackageSpec& p) const { return share_dir(p) / FileVcpkgSpdxJson; }
        Path vcpkg_port_config_cmake(const PackageSpec& p) const { return share_dir(p) / FileVcpkgPortConfig; }

    private:
        Path m_root;
    };
}
