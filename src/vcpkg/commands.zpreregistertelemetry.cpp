#include <vcpkg/commands.zpreregistertelemetry.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace
{
    static void set_define_metrics()
    {
        auto metrics = LockGuardPtr<Metrics>(g_metrics);
        for (auto metric : Metrics::get_define_metrics())
        {
            metrics->track_define_property(metric.metric);
        }
    }

    static void set_bool_metrics()
    {
        auto metrics = LockGuardPtr<Metrics>(g_metrics);
        for (auto metric : Metrics::get_bool_metrics())
        {
            metrics->track_bool_property(metric.metric, false);
        }
    }

    static void set_string_metrics()
    {
        auto metrics = LockGuardPtr<Metrics>(g_metrics);
        for (auto&& kv : Metrics::get_string_metrics_preregister_values())
        {
            metrics->track_string_property(kv.metric, kv.name.to_string());
        }
    }

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("z-preregister-telemetry"),
        0,
        0,
        {},
        nullptr,
    };
}

namespace vcpkg::Commands
{
    void ZPreRegisterTelemetryCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        (void)args;
        (void)fs;

        auto metrics_enabled = false;

        {
            auto metrics = LockGuardPtr<Metrics>(g_metrics);
            metrics->set_print_metrics(true);
            metrics_enabled = metrics->metrics_enabled();
        }

        if (metrics_enabled)
        {
            // fills the property message with dummy data
            // telemetry is uploaded via the usual mechanism
            set_define_metrics();
            set_bool_metrics();
            set_string_metrics();
        }
        else
        {
            msg::println_warning(msgVcpkgSendMetricsButDisabled);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
