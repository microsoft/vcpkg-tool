#pragma once

#include <vcpkg/base/files.h>
#include <vcpkg/base/lockguarded.h>
#include <vcpkg/base/util.h>

#include <string>

namespace vcpkg
{
    template<typename T>
    struct MetricEntry
    {
        T metric;
        StringLiteral name;
    };

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

    enum class StringMetric
    {
        BuildError,
        CommandArgs,
        CommandContext,
        CommandName,
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

    enum class BoolMetric
    {
        InstallManifestMode,
        OptionOverlayPorts,
        COUNT // always keep COUNT last
    };

    struct Metrics
    {
        Metrics() = default;
        Metrics(const Metrics&) = delete;
        Metrics& operator=(const Metrics&) = delete;

        void set_send_metrics(bool should_send_metrics);
        void set_print_metrics(bool should_print_metrics);

        // This function is static and must be called outside the g_metrics lock.
        static void enable();

        void track_metric(const std::string& name, double value);
        void track_buildtime(const std::string& name, double value);

        void track_define_property(DefineMetric metric);
        void track_string_property(StringMetric metric, StringView value);
        void track_bool_property(BoolMetric metric, bool value);

        void track_feature(const std::string& feature, bool value);

        bool metrics_enabled();

        void upload(const std::string& payload);
        void flush(Filesystem& fs);

        // exposed for testing
        static View<MetricEntry<DefineMetric>> get_define_metrics();
        static View<MetricEntry<StringMetric>> get_string_metrics();
        static View<MetricEntry<BoolMetric>> get_bool_metrics();
    };

    extern LockGuarded<Metrics> g_metrics;
}
