#include <vcpkg/commands.h>
#include <vcpkg/help.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/update.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Update
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
                const auto& latest_pgh = *p_scfl->source_control_file->core_paragraph;
                auto latest_version = Version(latest_pgh.raw_version, latest_pgh.port_version);
                auto installed_version = Version(pgh->package.version, pgh->package.port_version);
                if (latest_version != installed_version)
                {
                    output.push_back(
                        {pgh->package.spec, VersionDiff(std::move(installed_version), std::move(latest_version))});
                }
            }
            else
            {
                // No portfile available
            }
        }

        return output;
    }

    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("update"); },
        0,
        0,
        {},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        if (paths.manifest_mode_enabled())
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, msgUnsupportedUpdateCMD);
        }

        (void)args.parse_arguments(COMMAND_STRUCTURE);
        msg::println(msgLocalPortfileVersion);

        auto& fs = paths.get_filesystem();
        const StatusParagraphs status_db = database_load_check(fs, paths.installed());

        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(
            fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));

        const auto outdated_packages = SortedVector<OutdatedPackage, decltype(&OutdatedPackage::compare_by_name)>(
            find_outdated_packages(provider, status_db), &OutdatedPackage::compare_by_name);

        if (outdated_packages.empty())
        {
            msg::println(msgPackagesUpToDate);
        }
        else
        {
            msg::println(msgPortVersionConflict);
            for (auto&& package : outdated_packages)
            {
                msg::write_unlocalized_text_to_stdout(
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

    void UpdateCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Update::perform_and_exit(args, paths);
    }
}
