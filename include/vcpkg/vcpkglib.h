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

    std::vector<InstalledPackageView> get_installed_ports(const StatusParagraphs& status_db);
    std::vector<StatusParagraphAndAssociatedFiles> get_installed_files(const Filesystem& fs,
                                                                       const InstalledPaths& installed,
                                                                       const StatusParagraphs& status_db);

    std::string shorten_text(StringView desc, const size_t length);
} // namespace vcpkg
