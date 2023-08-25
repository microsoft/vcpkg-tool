#include <vcpkg/commands.z-upload-metrics.h>

#if defined(_WIN32)
#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>

#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    constexpr CommandMetadata CommandZUploadMetricsMetadata{
        "z-upload-metrics",
        {/*intentionally undocumented*/},
        {},
        AutocompletePriority::Never,
        1,
        1,
        {},
        nullptr,
    };

    void command_z_upload_metrics_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        const auto parsed = args.parse_arguments(CommandZUploadMetricsMetadata);
        const auto& payload_path = parsed.command_arguments[0];
        auto payload = fs.read_contents(payload_path, VCPKG_LINE_INFO);
        winhttp_upload_metrics(payload);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
#endif // defined(_WIN32)
