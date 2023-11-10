#pragma once

#include <vcpkg/fwd/portfileprovider.h>
#include <vcpkg/fwd/statusparagraphs.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/versions.h>

#include <vector>

namespace vcpkg
{
    struct OutdatedPackage
    {
        static bool compare_by_name(const OutdatedPackage& left, const OutdatedPackage& right);

        PackageSpec spec;
        VersionDiff version_diff;
    };

    std::vector<OutdatedPackage> find_outdated_packages(const PortFileProvider& provider,
                                                        const StatusParagraphs& status_db);

    extern const CommandMetadata CommandUpdateMetadata;
    void command_update_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
