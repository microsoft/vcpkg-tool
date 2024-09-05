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
        PackageSpec spec;
        VersionDiff version_diff;
    };

    struct OutdatedReport
    {
        std::vector<VersionedPackageSpec> up_to_date_packages;
        std::vector<OutdatedPackage> outdated_packages;
        std::vector<VersionedPackageSpec> missing_packages;
        std::vector<LocalizedString> parse_errors;
    };

    OutdatedReport build_outdated_report(const PortFileProvider& provider,
                                         View<VersionedPackageSpec> candidates_for_upgrade);
    OutdatedReport build_outdated_report(const PortFileProvider& provider, const StatusParagraphs& status_db);

    extern const CommandMetadata CommandUpdateMetadata;
    void command_update_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
