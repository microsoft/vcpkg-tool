#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.z-upload-metrics.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace
{
    constexpr CommandSwitch command_switches[] = {
        {SwitchUseTestData, {}},
    };
}

namespace vcpkg
{
    constexpr CommandMetadata CommandZUploadMetricsMetadata{
        "z-upload-metrics",
        {/*intentionally undocumented*/},
        {},
        Undocumented,
        AutocompletePriority::Never,
        0,
        1,
        {command_switches},
        nullptr,
    };

    void command_z_upload_metrics_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        g_should_send_metrics = false; // avoid recursion

        // note that z-upload-metrics is usually going to have metrics disabled as it is usually
        // invoked inside vcpkg, and we don't collect vcpkg-in-vcpkg metrics.
        const auto parsed = args.parse_arguments(CommandZUploadMetricsMetadata);

        std::string payload;
        if (Util::Sets::contains(parsed.switches, SwitchUseTestData))
        {
            MetricsUserConfig user{
                "00000000-0000-0000-0000-000000000000", // user_id
                CTime::now_string(),                    // user_time
                "0",                                    // user_mac
            };
            auto session = MetricsSessionData::from_system();
            MetricsSubmission submission;
            submission.track_string(StringMetric::CommandName, "z-upload-metrics-test-data");
            submission.track_string(StringMetric::DevDeviceId, "00000000-0000-0000-0000-000000000000");
            payload = format_metrics_payload(user, session, submission);
        }
        else
        {
            Checks::check_exit(
                VCPKG_LINE_INFO, !parsed.command_arguments.empty(), "Expected a payload file path argument");
            payload = fs.read_contents(parsed.command_arguments[0], VCPKG_LINE_INFO);
        }

        if (!curl_upload_metrics(payload))
        {
            Debug::println("Failed to upload metrics");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        if (!parsed.command_arguments.empty())
        {
            std::error_code ec;
            fs.remove(parsed.command_arguments[0], ec);
            if (ec)
            {
                Debug::println("Failed to remove file after upload: {}", ec.message());
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
