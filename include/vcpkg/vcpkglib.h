#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/installedpaths.h>
#include <vcpkg/fwd/statusparagraphs.h>

#include <vcpkg/statusparagraph.h>

#include <string>

namespace vcpkg
{
    // Read the status database
    StatusParagraphs database_load(const ReadOnlyFilesystem& fs, const InstalledPaths& installed);
    // Read the status database, and collapse update records into the current status file
    StatusParagraphs database_load_collapse(const Filesystem& fs, const InstalledPaths& installed);

    // Adds an update record
    void write_update(const Filesystem& fs, const InstalledPaths& installed, const StatusParagraph& p);

    struct StatusParagraphAndAssociatedFiles
    {
        StatusParagraph pgh;
        std::vector<std::string> files;
    };

    std::vector<InstalledPackageView> get_installed_ports(const StatusParagraphs& status_db);

    // Reads the installed files from the status database.
    std::vector<StatusParagraphAndAssociatedFiles> get_installed_files(const ReadOnlyFilesystem& fs,
                                                                       const InstalledPaths& installed,
                                                                       const StatusParagraphs& status_db);
    // Reads the installed files from the status database, converting installed file lists to the current version if
    // necessary.
    std::vector<StatusParagraphAndAssociatedFiles> get_installed_files_and_upgrade(const Filesystem& fs,
                                                                                   const InstalledPaths& installed,
                                                                                   const StatusParagraphs& status_db);

    std::string shorten_text(StringView desc, const size_t length);
} // namespace vcpkg
