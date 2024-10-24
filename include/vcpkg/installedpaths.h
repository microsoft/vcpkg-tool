#pragma once

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

        const Path& root() const { return m_root; }
        Path listfile_path(const BinaryParagraph& pgh) const;

        Path vcpkg_dir() const { return m_root / FileVcpkg; }
        Path vcpkg_dir_status_file() const { return vcpkg_dir() / FileStatus; }
        Path vcpkg_dir_info() const { return vcpkg_dir() / FileInfo; }
        Path vcpkg_dir_updates() const { return vcpkg_dir() / FileUpdates; }
        Path lockfile_path() const { return vcpkg_dir() / FileVcpkgLock; }
        Path triplet_dir(Triplet t) const { return m_root / t.canonical_name(); }
        Path share_dir(const PackageSpec& p) const { return triplet_dir(p.triplet()) / FileShare / p.name(); }
        Path usage_file(const PackageSpec& p) const { return share_dir(p) / FileUsage; }
        Path spdx_file(const PackageSpec& p) const { return share_dir(p) / FileVcpkgSpdxJson; }
        Path vcpkg_port_config_cmake(const PackageSpec& p) const { return share_dir(p) / FileVcpkgPortConfig; }

    private:
        Path m_root;
    };
}
