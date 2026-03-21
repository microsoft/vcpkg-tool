#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/installeddatabase.h>
#include <vcpkg/fwd/installedpaths.h>
#include <vcpkg/fwd/statusparagraphs.h>

#include <vcpkg/statusparagraph.h>

#include <memory>
#include <string>

namespace vcpkg
{
    // When vcpkg does anything that needs to look at an installed database, it must take this
    // filesystem lock to protect itself from concurrent modification of the same installed tree
    // by competing vcpkg instances.
    struct InstalledDatabaseLock
    {
        InstalledDatabaseLock(const Filesystem& fs,
                              const InstalledPaths& installed,
                              const Optional<bool>& wait_for_lock,
                              const Optional<bool>& ignore_lock_failures);
        ~InstalledDatabaseLock();

        InstalledDatabaseLock(const InstalledDatabaseLock&) = delete;
        InstalledDatabaseLock& operator=(const InstalledDatabaseLock&) = delete;

    private:
        std::unique_ptr<IExclusiveFileLock> m_lock;
    };

    // Read the status database
    StatusParagraphs database_load(const ReadOnlyFilesystem& fs,
                                   const InstalledPaths& installed,
                                   const InstalledDatabaseLock& /*witness*/);
    // Read the status database, and collapse update records into the current status file
    StatusParagraphs database_sync(const Filesystem& fs,
                                   const InstalledPaths& installed,
                                   const InstalledDatabaseLock& /*witness*/);

    // Adds an update record
    void database_write_update(const Filesystem& fs, const InstalledPaths& installed, const StatusParagraph& p);

    std::vector<InstalledPackageView> get_installed_ports(const StatusParagraphs& status_db);

    struct StatusParagraphAndAssociatedFiles
    {
        StatusParagraph pgh;
        std::vector<std::string> files;
    };

    // Reads the installed files from the status database.
    std::vector<StatusParagraphAndAssociatedFiles> get_installed_files(const ReadOnlyFilesystem& fs,
                                                                       const InstalledPaths& installed,
                                                                       const StatusParagraphs& status_db);
    // Reads the installed files from the status database, converting installed file lists to the current version if
    // necessary.
    std::vector<StatusParagraphAndAssociatedFiles> get_installed_files_and_upgrade(const Filesystem& fs,
                                                                                   const InstalledPaths& installed,
                                                                                   const StatusParagraphs& status_db);
} // namespace vcpkg
