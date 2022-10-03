#pragma once

#include <vcpkg/base/files.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/view.h>

#include <array>
#include <atomic>
#include <map>
#include <set>
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

    // Batches metrics changes so they can be submitted under a single lock acquisition or
    // in a single JSON payload.
    struct MetricsSubmission
    {
        void track_elapsed_us(double value);
        void track_buildtime(StringView name, double value);
        void track_define_property(DefineMetric metric);
        void track_string_property(StringMetric metric, StringView value);
        void track_bool_property(BoolMetric metric, bool value);
        void track_feature(StringView feature, bool value);

        std::vector<double> elapsed_us;
        std::map<std::string, double, std::less<>> buildtimes;
        std::set<DefineMetric> define_properties;
        std::map<StringMetric, std::string> string_properties;
        std::map<BoolMetric, bool> bool_properties;
        std::map<std::string, bool, std::less<>> feature_metrics;
    };

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

    struct MetricsUserConfig
    {
        std::string user_id;
        std::string user_time;
        std::string user_mac;

        std::string last_completed_survey;

        void to_string(std::string&) const;
        std::string to_string() const;
        void try_write(Filesystem& fs) const;

        // If *this is missing data normally provided by the system, fill it in;
        // otherwise, no effects.
        // Returns whether any values needed to be modified.
        bool fill_in_system_values();
    };

    MetricsUserConfig try_parse_metrics_user(StringView content);
    MetricsUserConfig try_read_metrics_user(const Filesystem& fs);

    extern std::atomic<bool> g_metrics_enabled;
    extern std::atomic<bool> g_should_print_metrics;
    extern std::atomic<bool> g_should_send_metrics;

    void enable_global_metrics(Filesystem&);
    void flush_global_metrics(Filesystem&);
#if defined(_WIN32)
    void winhttp_upload_metrics(StringView payload);
#endif // ^^^ _WIN32
}
