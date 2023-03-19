#include <vcpkg/commands.upload-metrics.h>

#if defined(_WIN32)
#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>

#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace vcpkg::Commands::UploadMetrics
{
    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("x-upload-metrics metrics.txt"); },
        1,
        1,
    };

    void UploadMetricsCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        const auto parsed = args.parse_arguments(COMMAND_STRUCTURE);
        const auto& payload_path = parsed.command_arguments[0];
        auto payload = fs.read_contents(payload_path, VCPKG_LINE_INFO);
        winhttp_upload_metrics(payload);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
#endif // defined(_WIN32)
