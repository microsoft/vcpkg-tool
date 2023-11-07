#pragma once

namespace vcpkg
{
    enum class BuildResult
    {
        SUCCEEDED,
        BUILD_FAILED,
        POST_BUILD_CHECKS_FAILED,
        FILE_CONFLICTS,
        CASCADED_DUE_TO_MISSING_DEPENDENCIES,
        EXCLUDED,
        CACHE_MISSING,
        DOWNLOADED,
        REMOVED
    };

    enum class UseHeadVersion
    {
        NO = 0,
        YES
    };

    enum class AllowDownloads
    {
        NO = 0,
        YES
    };

    enum class OnlyDownloads
    {
        NO = 0,
        YES
    };

    enum class CleanBuildtrees
    {
        NO = 0,
        YES
    };

    enum class CleanPackages
    {
        NO = 0,
        YES
    };

    enum class CleanDownloads
    {
        NO = 0,
        YES
    };

    enum class ConfigurationType
    {
        BOTH,
        DEBUG,
        RELEASE,
    };

    enum class DownloadTool
    {
        BUILT_IN,
        ARIA2,
    };
    enum class PurgeDecompressFailure
    {
        NO = 0,
        YES
    };

    enum class Editable
    {
        NO = 0,
        YES
    };

    enum class BackcompatFeatures
    {
        ALLOW = 0,
        PROHIBIT
    };

    enum class BuildMissing
    {
        NO = 0,
        YES
    };

    enum class PrintUsage
    {
        YES = 0,
        NO
    };

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
