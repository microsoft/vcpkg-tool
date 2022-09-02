#pragma once

#include <vcpkg/base/files.h>
#include <vcpkg/base/lockguarded.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/util.h>

#include <array>
#include <string>

namespace vcpkg
{
    enum class DefineMetric
    {
        AssetSource,
        BinaryCachingAws,
        BinaryCachingAzBlob,
        BinaryCachingCos,
        BinaryCachingDefault,
        BinaryCachingFiles,
        BinaryCachingGcs,
        BinaryCachingHttp,
        BinaryCachingNuget,
        BinaryCachingSource,
        ErrorVersioningDisabled,
        ErrorVersioningNoBaseline,
        GitHubRepository,
        ManifestBaseline,
        ManifestOverrides,
        ManifestVersionConstraint,
        RegistriesErrorCouldNotFindBaseline,
        RegistriesErrorNoVersionsAtCommit,
        VcpkgBinarySources,
        VcpkgDefaultBinaryCache,
        VcpkgNugetRepository,
        VersioningErrorBaseline,
        VersioningErrorVersion,
        X_VcpkgRegistriesCache,
        X_WriteNugetPackagesConfig,
        COUNT // always keep COUNT last
    };

    struct DefineMetricEntry
    {
        DefineMetric metric;
        StringLiteral name;
    };

    extern const std::array<DefineMetricEntry, static_cast<size_t>(DefineMetric::COUNT)> all_define_metrics;

    enum class StringMetric
    {
        BuildError,
        CommandArgs,
        CommandContext,
        CommandName,
        DetectedCiEnvironment,
        Error,
        InstallPlan_1,
        ListFile,
        RegistriesDefaultRegistryKind,
        RegistriesKindsUsed,
        Title,
        UserMac,
        VcpkgVersion,
        Warning,
        COUNT // always keep COUNT last
    };

    struct StringMetricEntry
    {
        StringMetric metric;
        StringLiteral name;
        StringLiteral preregister_value; // mock values
    };

    extern const std::array<StringMetricEntry, static_cast<size_t>(StringMetric::COUNT)> all_string_metrics;

    enum class BoolMetric
    {
        DetectedContainer,
        InstallManifestMode,
        OptionOverlayPorts,
        COUNT // always keep COUNT last
    };

    struct BoolMetricEntry
    {
        BoolMetric metric;
        StringLiteral name;
    };

    extern const std::array<BoolMetricEntry, static_cast<size_t>(BoolMetric::COUNT)> all_bool_metrics;

    struct Metrics
    {
        Metrics() = default;
        Metrics(const Metrics&) = delete;
        Metrics& operator=(const Metrics&) = delete;

        static void set_send_metrics(bool should_send_metrics);
        static void set_print_metrics(bool should_print_metrics);

        // This function is static and must be called outside the g_metrics lock.
        static void enable();

        static void track_metric(const std::string& name, double value);
        static void track_buildtime(const std::string& name, double value);

        static void track_define_property(DefineMetric metric);
        static void track_string_property(StringMetric metric, StringView value);
        static void track_bool_property(BoolMetric metric, bool value);

        static void track_feature(const std::string& feature, bool value);

        static bool metrics_enabled();

        static void upload(const std::string& payload);
        static void flush(Filesystem& fs);
    };

    extern LockGuarded<Metrics> g_metrics;
}
