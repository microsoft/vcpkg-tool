#pragma once

#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/lockguarded.h>
#include <vcpkg/base/util.h>

#include <string>

namespace vcpkg
{
    struct Metrics
    {
        enum class SetMetric
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
            X_WriteNugetPackagesConfig
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
            Warning
        };

        enum class BoolMetric
        {
            InstallManifestMode,
            OptionOverlayPorts
        };

        Metrics() = default;
        Metrics(const Metrics&) = delete;
        Metrics& operator=(const Metrics&) = delete;

        void set_send_metrics(bool should_send_metrics);
        void set_print_metrics(bool should_print_metrics);

        // This function is static and must be called outside the g_metrics lock.
        static void enable();

        void track_metric(const std::string& name, double value);
        void track_buildtime(const std::string& name, double value);

        void track_property(SetMetric metric);
        void track_property(StringMetric metric, const std::string& value);
        void track_property(BoolMetric metric, bool value);

        void track_feature(const std::string& feature, bool value);

        bool metrics_enabled();

        void upload(const std::string& payload);
        void flush(Filesystem& fs);
    };

    extern LockGuarded<Metrics> g_metrics;
}
