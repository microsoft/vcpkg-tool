#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/base/stringview.h>

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace vcpkg
{
    enum class DefineMetric
    {
        AssetSource,
        BinaryCachingAws,
        BinaryCachingAzBlob,
        BinaryCachingAzCopy,
        BinaryCachingAzCopySas,
        BinaryCachingCos,
        BinaryCachingDefault,
        BinaryCachingFiles,
        BinaryCachingGcs,
        BinaryCachingHttp,
        BinaryCachingNuget,
        BinaryCachingSource,
        BinaryCachingUpkg,
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
        VersioningErrorVersion, // no longer used
        X_VcpkgRegistriesCache,
        X_WriteNugetPackagesConfig,
        COUNT // always keep COUNT last
    };

    struct DefineMetricEntry
    {
        DefineMetric metric;
        StringLiteral name;
    };

    extern const DefineMetricEntry all_define_metrics[static_cast<size_t>(DefineMetric::COUNT)];

    enum class StringMetric
    {
        AcquiredArtifacts,
        ActivatedArtifacts,
        CiOwnerId,
        CiProjectId,
        CommandArgs,
        CommandContext,
        CommandName,
        DeploymentKind,
        DetectedCiEnvironment,
        DetectedLibCurlVersion,
        DevDeviceId,
        ExitCode,
        ExitLocation,
        InstallPlan_1,
        ListFile,
        ProcessTree,
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

    extern const StringMetricEntry all_string_metrics[static_cast<size_t>(StringMetric::COUNT)];

    enum class BoolMetric
    {
        DetectedContainer,
        DependencyGraphSuccess,
        FeatureFlagBinaryCaching,
        FeatureFlagCompilerTracking,
        FeatureFlagDependencyGraph,
        FeatureFlagManifests,
        FeatureFlagRegistries,
        FeatureFlagVersions,
        InstallManifestMode,
        OptionOverlayPorts,
        COUNT // always keep COUNT last
    };

    struct BoolMetricEntry
    {
        BoolMetric metric;
        StringLiteral name;
    };

    extern const BoolMetricEntry all_bool_metrics[static_cast<size_t>(BoolMetric::COUNT)];

    // Batches metrics changes so they can be submitted under a single lock acquisition or
    // in a single JSON payload.
    struct MetricsSubmission
    {
        void track_elapsed_us(double value);
        void track_buildtime(StringView name, double value);
        void track_define(DefineMetric metric);
        void track_string(StringMetric metric, StringView value);
        void track_bool(BoolMetric metric, bool value);
        void merge(MetricsSubmission&& other);

        double elapsed_us = 0.0;
        std::map<std::string, double, std::less<>> buildtimes;
        std::set<DefineMetric> defines;
        std::map<StringMetric, std::string> strings;
        std::map<BoolMetric, bool> bools;
    };

    // Collects metrics, potentially from multiple threads.
    // Member functions of this type are safe to call from multiple threads, and will
    // be observed in a total order.
    struct MetricsCollector
    {
        MetricsCollector() = default;
        MetricsCollector(const MetricsCollector&) = delete;
        MetricsCollector& operator=(const MetricsCollector&) = delete;
        ~MetricsCollector() = default;

        // Track
        void track_elapsed_us(double value);
        void track_buildtime(StringView name, double value);
        void track_define(DefineMetric metric);
        void track_string(StringMetric metric, StringView value);
        void track_bool(BoolMetric metric, bool value);
        void track_submission(MetricsSubmission&& submission_);

        // Consume
        MetricsSubmission get_submission() const;

    private:
        mutable std::mutex mtx;
        MetricsSubmission submission;
    };

    MetricsCollector& get_global_metrics_collector() noexcept; // Meyers singleton

    struct MetricsUserConfig
    {
        std::string user_id;
        std::string user_time;
        std::string user_mac;

        std::string last_completed_survey;

        void to_string(std::string&) const;
        std::string to_string() const;
        void try_write(const Filesystem& fs) const;

        // If *this is missing data normally provided by the system, fill it in;
        // otherwise, no effects.
        // Returns whether any values needed to be modified.
        bool fill_in_system_values();
    };

    MetricsUserConfig try_parse_metrics_user(StringView content);
    MetricsUserConfig try_read_metrics_user(const ReadOnlyFilesystem& fs);

    struct MetricsSessionData
    {
        std::string submission_time;
        std::string os_version;
        std::string session_id;
        std::string parent_process_list;

        static MetricsSessionData from_system();
    };

    std::string format_metrics_payload(const MetricsUserConfig& user,
                                       const MetricsSessionData& session,
                                       const MetricsSubmission& submission);

    extern std::atomic<bool> g_metrics_enabled;
    extern std::atomic<bool> g_should_print_metrics;
    extern std::atomic<bool> g_should_send_metrics;

    void flush_global_metrics(const Filesystem&);
#if defined(_WIN32)
    void winhttp_upload_metrics(StringView payload);
#endif // ^^^ _WIN32
}
