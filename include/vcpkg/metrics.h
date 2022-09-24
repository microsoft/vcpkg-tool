#pragma once

#include <vcpkg/base/files.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/view.h>

#include <array>
#include <atomic>
#include <string>
#include <utility>
#include <vector>

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

    struct MetricsSubmission;

    // This interface collects metrics submitted by other parts of the codebase.
    // Implementations are expected to be safe to call from multiple threads.
    struct MetricsCollector
    {
        virtual void track_elapsed_us(double value) = 0;
        virtual void track_buildtime(StringView name, double value) = 0;

        virtual void track_define_property(DefineMetric metric) = 0;
        virtual void track_string_property(StringMetric metric, StringView value) = 0;
        virtual void track_bool_property(BoolMetric metric, bool value) = 0;

        virtual void track_feature(StringView feature, bool value) = 0;

        virtual void track_submission(MetricsSubmission&& submission) = 0;

    protected:
        MetricsCollector();
        MetricsCollector(const MetricsCollector&) = delete;
        MetricsCollector& operator=(const MetricsCollector&) = delete;
        ~MetricsCollector();
    };

    MetricsCollector& get_global_metrics_collector() noexcept;

    struct MetricsCollectorImpl;

    // Batches metrics changes so they can be submitted under a single lock acquisition.
    struct MetricsSubmission
    {
        friend MetricsCollectorImpl;

        MetricsSubmission(MetricsCollector& target_) : target(target_) { }
        MetricsSubmission(const MetricsSubmission&) = delete;
        MetricsSubmission& operator=(const MetricsSubmission&) = delete;
        ~MetricsSubmission() { target.track_submission(std::move(*this)); }

        void track_elapsed_us(double value) { elapsed_us.push_back(value); }

        void track_buildtime(StringView name, double value)
        {
            buildtimes.emplace_back(std::piecewise_construct,
                                    std::forward_as_tuple(name.data(), name.size()),
                                    std::forward_as_tuple(value));
        }

        void track_define_property(DefineMetric metric) { define_properties.push_back(metric); }

        void track_string_property(StringMetric metric, StringView value)
        {
            string_properties.emplace_back(std::piecewise_construct,
                                           std::forward_as_tuple(metric),
                                           std::forward_as_tuple(value.data(), value.size()));
        }

        void track_bool_property(BoolMetric metric, bool value)
        {
            bool_properties.insert(std::map<BoolMetric, bool>::value_type(metric, value));
        }

        void track_feature(StringView feature, bool value)
        {
            feature_metrics.emplace_back(std::piecewise_construct,
                                         std::forward_as_tuple(feature.data(), feature.size()),
                                         std::forward_as_tuple(value));
        }

    private:
        MetricsCollector& target;
        std::vector<double> elapsed_us;
        std::vector<std::pair<std::string, double>> buildtimes;
        std::vector<DefineMetric> define_properties;
        std::vector<std::pair<StringMetric, std::string>> string_properties;
        std::map<BoolMetric, bool> bool_properties;
        std::vector<std::pair<std::string, bool>> feature_metrics;
    };

    extern std::atomic<bool> g_metrics_enabled;
    extern std::atomic<bool> g_should_print_metrics;
    extern std::atomic<bool> g_should_send_metrics;

    void enable_global_metrics();
    void flush_global_metrics(Filesystem&);
#if defined(_WIN32)
    void winhttp_upload_metrics(StringView payload);
#endif // ^^^ _WIN32
}
