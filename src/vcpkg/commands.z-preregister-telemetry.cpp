#include <vcpkg/commands.z-preregister-telemetry.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace
{
    static void set_define_metrics(MetricsSubmission& metrics)
    {
        for (auto&& metric : all_define_metrics)
        {
            metrics.track_define(metric.metric);
        }
    }

    static void set_bool_metrics(MetricsSubmission& metrics)
    {
        for (auto&& metric : all_bool_metrics)
        {
            metrics.track_bool(metric.metric, false);
        }
    }

    static void set_string_metrics(MetricsSubmission& metrics)
    {
        for (auto&& metric : all_string_metrics)
        {
            metrics.track_string(metric.metric, metric.preregister_value);
        }
    }
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandZPreregisterTelemetryMetadata{
        "z-preregister-telemetry",
        {/*intentionally undocumented*/},
        {},
        Undocumented,
        AutocompletePriority::Never,
        0,
        0,
        {},
        nullptr,
    };

    void command_z_preregister_telemetry_and_exit(const VcpkgCmdArguments& args, const Filesystem&)
    {
        (void)args.parse_arguments(CommandZPreregisterTelemetryMetadata);
        auto metrics_enabled = g_metrics_enabled.load();
        if (metrics_enabled)
        {
            // fills the property message with dummy data
            // telemetry is uploaded via the usual mechanism
            g_should_print_metrics = true;
            MetricsSubmission metrics;
            set_define_metrics(metrics);
            set_bool_metrics(metrics);
            set_string_metrics(metrics);
            get_global_metrics_collector().track_submission(std::move(metrics));
        }
        else
        {
            msg::write_unlocalized_text_to_stderr(
                Color::warning,
                LocalizedString::from_raw(WarningPrefix).append(msgVcpkgSendMetricsButDisabled).append_raw('\n'));
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
