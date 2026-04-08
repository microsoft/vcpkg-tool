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
    struct InstalledDatabaseLockImpl
    {
        // Do not use directly, use InstalledDatabaseLock or InstallAndBuildDatabaseLock instead.
        InstalledDatabaseLockImpl(const Filesystem& fs,
                                  const InstalledPaths& installed,
                                  const std::vector<Path>& extra_lock_files,
                                  const Optional<bool>& wait_for_lock,
                                  const Optional<bool>& ignore_lock_failures);

        InstalledDatabaseLockImpl(const InstalledDatabaseLockImpl&) = delete;
        InstalledDatabaseLockImpl& operator=(const InstalledDatabaseLockImpl&) = delete;
        ~InstalledDatabaseLockImpl();

    protected:
        std::vector<std::unique_ptr<IExclusiveFileLock>> m_locks;
    };

    struct InstalledDatabaseLock : InstalledDatabaseLockImpl
    {
        InstalledDatabaseLock(const Filesystem& fs,
                              const InstalledPaths& installed,
                              const Optional<bool>& wait_for_lock,
                              const Optional<bool>& ignore_lock_failures);

        InstalledDatabaseLock(const InstalledDatabaseLock&) = delete;
        InstalledDatabaseLock& operator=(const InstalledDatabaseLock&) = delete;
        ~InstalledDatabaseLock();

    protected:
        using InstalledDatabaseLockImpl::InstalledDatabaseLockImpl;
    };

    struct InstallAndBuildDatabaseLock : InstalledDatabaseLock
    {
        // When buildtrees and packages are also used. TODO: Ideally a separate lock infrastructure would cover this.
        // It's merged into InstalledDatabaseLock for the moment to force locks to be taken in the correct order.
        InstallAndBuildDatabaseLock(const Filesystem& fs,
                                    const InstalledPaths& installed,
                                    const Path& buildtrees,
                                    const Path& packages,
                                    const Optional<bool>& wait_for_lock,
                                    const Optional<bool>& ignore_lock_failures);

        InstallAndBuildDatabaseLock(const InstallAndBuildDatabaseLock&) = delete;
        InstallAndBuildDatabaseLock& operator=(const InstallAndBuildDatabaseLock&) = delete;
        ~InstallAndBuildDatabaseLock();
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
