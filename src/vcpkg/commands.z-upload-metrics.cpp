#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>

#include <vcpkg/commands.z-upload-metrics.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    constexpr CommandSwitch UPLOAD_SWITCHES[] = {
        {SwitchDeleteFileAfterUpload, msgCmdUploadMetricsDeleteFileAfterUpload},
    };

    constexpr CommandMetadata CommandZUploadMetricsMetadata{
        "z-upload-metrics",
        {/*intentionally undocumented*/},
        {},
        Undocumented,
        AutocompletePriority::Never,
        1,
        1,
        {UPLOAD_SWITCHES},
        nullptr,
    };

    void command_z_upload_metrics_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        const auto parsed = args.parse_arguments(CommandZUploadMetricsMetadata);
        const auto& payload_path = parsed.command_arguments[0];
        auto payload = fs.read_contents(payload_path, VCPKG_LINE_INFO);
        auto success = curl_upload_metrics(payload);
        if (success)
        {
            if (parsed.switches.find(SwitchDeleteFileAfterUpload) != parsed.switches.end())
            {
                std::error_code ec;
                fs.remove(payload_path, ec);
#ifndef NDEBUG
                if (ec) fprintf(stderr, "[DEBUG] Failed to remove file after upload: %s\n", ec.message().c_str());
#endif // NDEBUG
            }
        }
#ifndef NDEBUG
        else
        {
            fprintf(stderr, "[DEBUG] Failed to upload metrics\n");
        }
#endif // NDEBUG

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
