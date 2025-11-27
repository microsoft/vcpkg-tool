#pragma once

namespace vcpkg
{
    enum class InstallResult
    {
        FILE_CONFLICTS,
        SUCCESS,
    };

    enum class SymlinkHydrate
    {
        // If a symlink is detected, create a new symlink with the same path
        CopySymlinks,
        // If a symlink is detected, follow it and copy the data to which it refers
        CopyData,
    };

    struct SpecSummary;
    struct AbiSpecSummary;
    struct InstallSpecSummary;
    struct LicenseReport;
    struct InstallSummary;
    struct CMakeUsageInfo;
}
