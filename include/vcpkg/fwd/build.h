#pragma once

namespace vcpkg
{
    enum class BuildResult
    {
        Succeeded,
        BuildFailed,
        PostBuildChecksFailed,
        FileConflicts,
        CascadedDueToMissingDependencies,
        Excluded,
        CacheMissing,
        Downloaded,
        Removed
    };

    enum class UseHeadVersion
    {
        No = 0,
        Yes
    };

    enum class AllowDownloads
    {
        No = 0,
        Yes
    };

    enum class OnlyDownloads
    {
        No = 0,
        Yes
    };

    enum class CleanBuildtrees
    {
        No = 0,
        Yes
    };

    enum class CleanPackages
    {
        No = 0,
        Yes
    };

    enum class CleanDownloads
    {
        No = 0,
        Yes
    };

    enum class ConfigurationType
    {
        Debug,
        Release,
    };

    enum class Editable
    {
        No = 0,
        Yes
    };

    enum class BackcompatFeatures
    {
        Allow = 0,
        Prohibit
    };

    enum class BuildMissing
    {
        No = 0,
        Yes
    };

    enum class PrintUsage
    {
        No = 0,
        Yes
    };

    enum class KeepGoing
    {
        No = 0,
        Yes
    };

    // These names are intended to match VCPKG_POLICY_Xxx constants settable in portfile.cmake
    enum class BuildPolicy
    {
        EMPTY_PACKAGE,
        DLLS_WITHOUT_LIBS,
        DLLS_WITHOUT_EXPORTS,
        DLLS_IN_STATIC_LIBRARY,
        MISMATCHED_NUMBER_OF_BINARIES,
        ONLY_RELEASE_CRT,
        EMPTY_INCLUDE_FOLDER,
        ALLOW_OBSOLETE_MSVCRT,
        ALLOW_RESTRICTED_HEADERS,
        SKIP_DUMPBIN_CHECKS,
        SKIP_ARCHITECTURE_CHECK,
        CMAKE_HELPER_PORT,
        SKIP_ABSOLUTE_PATHS_CHECK,
        SKIP_ALL_POST_BUILD_CHECKS,
        SKIP_APPCONTAINER_CHECK,
        SKIP_CRT_LINKAGE_CHECK,
        SKIP_MISPLACED_CMAKE_FILES_CHECK,
        SKIP_LIB_CMAKE_MERGE_CHECK,
        ALLOW_DLLS_IN_LIB,
        SKIP_MISPLACED_REGULAR_FILES_CHECK,
        SKIP_COPYRIGHT_CHECK,
        ALLOW_KERNEL32_FROM_XBOX,
        ALLOW_EXES_IN_BIN,
        SKIP_USAGE_INSTALL_CHECK,
        ALLOW_EMPTY_FOLDERS,
        ALLOW_DEBUG_INCLUDE,
        ALLOW_DEBUG_SHARE,
        SKIP_PKGCONFIG_CHECK,
        // Must be last
        COUNT,
    };

    struct IBuildLogsRecorder;
    struct BuildPackageOptions;
    struct BuildResultCounts;
    struct PreBuildInfo;
    struct ExtendedBuildResult;
    struct BuildInfo;
    struct AbiEntry;
    struct CompilerInfo;
    struct AbiInfo;
    struct EnvCache;
    struct BuildCommand;
}
