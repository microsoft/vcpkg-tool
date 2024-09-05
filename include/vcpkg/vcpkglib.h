#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/installedpaths.h>
#include <vcpkg/fwd/statusparagraphs.h>

#include <vcpkg/base/sortedvector.h>

#include <vcpkg/statusparagraph.h>

#include <string>

namespace vcpkg
{
    StatusParagraphs database_load_check(const Filesystem& fs, const InstalledPaths& installed);

    void write_update(const Filesystem& fs, const InstalledPaths& installed, const StatusParagraph& p);

    struct StatusParagraphAndAssociatedFiles
    {
        StatusParagraph pgh;
        SortedVector<std::string> files;
    };

    std::map<PackageSpec, InstalledPackageView> get_installed_ports(const StatusParagraphs& status_db);
    // Create a vector of versioned package specs of packages installed according to `status_db`, sorted by package
    // spec.
    std::vector<VersionedPackageSpec> get_installed_port_version_specs(const StatusParagraphs& status_db);
    std::vector<VersionedPackageSpec> convert_installed_ports_to_versioned_specs(
        std::map<PackageSpec, InstalledPackageView>&& installed_ports);
    std::vector<StatusParagraphAndAssociatedFiles> get_installed_files(const Filesystem& fs,
                                                                       const InstalledPaths& installed,
                                                                       const StatusParagraphs& status_db);

    std::string shorten_text(StringView desc, const size_t length);
} // namespace vcpkg
