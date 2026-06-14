#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>

#include <vcpkg/commands.install.h>
#include <vcpkg/commands.usage-info.h>
#include <vcpkg/input.h>
#include <vcpkg/installeddatabase.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <map>

using namespace vcpkg;

namespace
{
    constexpr CommandSwitch SWITCHES[] = {{SwitchGenerated}, {SwitchForceAccurate}};
}

namespace vcpkg
{

    constexpr CommandMetadata CommandUsageInfoMetadata{
        "x-usage-info",
        "Prints usage information for a single installed port",
        {"vcpkg x-usage-info zlib:x64-windows"},
        Undocumented,
        AutocompletePriority::Internal,
        1,
        1,
        {SWITCHES},
        nullptr,
    };

    void command_usage_info_and_exit(const VcpkgCmdArguments& args,
                                     const VcpkgPaths& paths,
                                     Triplet default_triplet,
                                     Triplet host_triplet)
    {
        (void)host_triplet;
        msg::default_output_stream = OutputStream::StdErr;
        const ParsedArguments options = args.parse_arguments(CommandUsageInfoMetadata);
        const bool generated_only = Util::Sets::contains(options.switches, SwitchGenerated);
        const bool force_accurate = Util::Sets::contains(options.switches, SwitchForceAccurate);

        const FullPackageSpec spec =
            check_and_get_full_package_spec(options.command_arguments[0], default_triplet, paths.get_triplet_db())
                .value_or_exit(VCPKG_LINE_INFO);

        auto& fs = paths.get_filesystem();
        InstalledDatabaseLock installed_lock{fs, paths.installed(), args.wait_for_lock, args.ignore_lock_failures};
        const StatusParagraphs status_db = database_load(fs, paths.installed(), installed_lock);

        if (const auto it = status_db.find_installed(spec.package_spec); it != status_db.end())
        {
            const auto& bpgh = it->get()->package;
            const auto usage = generated_only
                                   ? get_cmake_usage_from_generated(fs, paths.installed(), bpgh, force_accurate)
                                   : get_cmake_usage(fs, paths.installed(), bpgh, force_accurate);
            if (!usage.message.empty())
            {
                msg::write_unlocalized_text(Color::none, usage.message);
            }
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        exit_with_package_not_installed(VCPKG_LINE_INFO, status_db, spec.package_spec);
    }
} // namespace vcpkg
