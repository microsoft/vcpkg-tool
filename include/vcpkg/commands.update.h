#pragma once

#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/portfileprovider.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/versions.h>

namespace vcpkg::Update
{
    struct OutdatedPackage
    {
        static bool compare_by_name(const OutdatedPackage& left, const OutdatedPackage& right);

        PackageSpec spec;
        VersionDiff version_diff;
    };

    std::vector<OutdatedPackage> find_outdated_packages(const PortFileProvider& provider,
                                                        const StatusParagraphs& status_db);

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
