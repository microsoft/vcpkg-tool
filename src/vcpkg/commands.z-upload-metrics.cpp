#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/system.debug.h>

#include <vcpkg/commands.z-upload-metrics.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    constexpr CommandMetadata CommandZUploadMetricsMetadata{
        "z-upload-metrics",
        {/*intentionally undocumented*/},
        {},
        Undocumented,
        AutocompletePriority::Never,
        1,
        1,
        {},
        nullptr,
    };

    void command_z_upload_metrics_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        g_should_send_metrics = false; // avoid recursion

        // note that z-upload-metrics is usually going to have metrics disabled as it is usually
        // invoked inside vcpkg, and we don't collect vcpkg-in-vcpkg metrics.
        const auto parsed = args.parse_arguments(CommandZUploadMetricsMetadata);
        const auto& payload_path = parsed.command_arguments[0];
        auto payload = fs.read_contents(payload_path, VCPKG_LINE_INFO);
        if (!curl_upload_metrics(payload))
        {
            Debug::println("Failed to upload metrics");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        std::error_code ec;
        fs.remove(payload_path, ec);
        if (ec)
        {
            Debug::println("Failed to remove file after upload: {}", ec.message());
        }
        
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
