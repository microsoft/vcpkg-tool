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
    bool OutdatedPackage::compare_by_name(const OutdatedPackage& left, const OutdatedPackage& right)
    {
        return left.spec.name() < right.spec.name();
    }

    std::vector<OutdatedPackage> find_outdated_packages(const PortFileProvider& provider,
                                                        const StatusParagraphs& status_db)
    {
        auto installed_packages = get_installed_ports(status_db);

        std::vector<OutdatedPackage> output;
        for (auto&& ipv : installed_packages)
        {
            const auto& pgh = ipv.core;
            auto maybe_scfl = provider.get_control_file(pgh->package.spec.name());
            if (auto p_scfl = maybe_scfl.get())
            {
                const auto& latest_version = p_scfl->to_version();
                if (latest_version != pgh->package.version)
                {
                    output.push_back({pgh->package.spec, VersionDiff(pgh->package.version, latest_version)});
                }
            }
            else
            {
                // No portfile available
            }
        }

        return output;
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
        const StatusParagraphs status_db = database_load(fs, paths.installed());

        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set, make_overlay_provider(fs, paths.overlay_ports));

        auto outdated_packages = find_outdated_packages(provider, status_db);
        Util::sort(outdated_packages, &OutdatedPackage::compare_by_name);
        if (outdated_packages.empty())
        {
            msg::println(msgPackagesUpToDate);
        }
        else
        {
            msg::println(msgPortVersionConflict);
            for (auto&& package : outdated_packages)
            {
                msg::write_unlocalized_text(
                    Color::none, fmt::format("\t{:<32} {}\n", package.spec, package.version_diff.to_string()));
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
