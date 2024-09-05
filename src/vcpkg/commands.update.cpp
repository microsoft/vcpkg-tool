#include <vcpkg/base/files.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.update.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg
{
    OutdatedReport build_outdated_report(const PortFileProvider& provider,
                                         View<VersionedPackageSpec> candidates_for_upgrade)
    {
        OutdatedReport results;
        for (auto&& candidate : candidates_for_upgrade)
        {
            auto maybe_scfl = provider.get_control_file(candidate.name());
            if (auto scfl = maybe_scfl.get())
            {
                if (auto scf = scfl->source_control_file.get())
                {
                    const auto& installed_version = candidate.version();
                    const auto& latest_version = scfl->to_version();
                    if (candidate.version() == latest_version)
                    {
                        results.up_to_date_packages.push_back(candidate);
                        continue;
                    }

                    results.outdated_packages.push_back({PackageSpec(candidate.name(), candidate.triplet()),
                                                         VersionDiff(installed_version, latest_version)});
                    continue;
                }

                results.missing_packages.push_back(candidate);
                continue;
            }

            results.parse_errors.push_back(std::move(maybe_scfl).error());
        }

        return results;
    }

    OutdatedReport build_outdated_report(const PortFileProvider& provider, const StatusParagraphs& status_db)
    {
        return build_outdated_report(provider, get_installed_port_version_specs(status_db));
    }

    constexpr CommandMetadata CommandUpdateMetadata{
        "update",
        msgHelpUpdateCommand,
        {"vcpkg update"},
        "https://learn.microsoft.com/vcpkg/commands/update",
        AutocompletePriority::Public,
        0,
        0,
        {},
        nullptr,
    };

    void command_update_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        if (paths.manifest_mode_enabled())
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, msgUnsupportedUpdateCMD);
        }

        (void)args.parse_arguments(CommandUpdateMetadata);
        msg::println(msgLocalPortfileVersion);

        auto& fs = paths.get_filesystem();
        const StatusParagraphs status_db = database_load_check(fs, paths.installed());

        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set,
                                       make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));

        const auto outdated_report = build_outdated_report(provider, status_db);
        if (outdated_report.outdated_packages.empty())
        {
            msg::println(msgPackagesUpToDate);
        }
        else
        {
            msg::println(msgPortVersionConflict);
            for (auto&& package : outdated_report.outdated_packages)
            {
                msg::write_unlocalized_text(Color::none,
                                            fmt::format("\t{:<32} {}\n", package.spec, package.version_diff));
            }

#if defined(_WIN32)
            auto vcpkg_cmd = ".\\vcpkg";
#else
            auto vcpkg_cmd = "./vcpkg";
#endif
            msg::println(msgToUpdatePackages, msg::command_name = vcpkg_cmd);
            msg::println(msgToRemovePackages, msg::command_name = vcpkg_cmd);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
