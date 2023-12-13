#include <vcpkg/base/messages.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/commands.update.h>
#include <vcpkg/commands.upgrade.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/input.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral OPTION_NO_DRY_RUN = "no-dry-run";
    constexpr StringLiteral OPTION_NO_KEEP_GOING = "no-keep-going";
    constexpr StringLiteral OPTION_ALLOW_UNSUPPORTED_PORT = "allow-unsupported";

    constexpr CommandSwitch SWITCHES[] = {
        {OPTION_NO_DRY_RUN, msgCmdUpgradeOptNoDryRun},
        {OPTION_NO_KEEP_GOING, msgCmdUpgradeOptNoKeepGoing},
        {OPTION_ALLOW_UNSUPPORTED_PORT, msgHelpTxtOptAllowUnsupportedPort},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandUpgradeMetadata = {
        "upgrade",
        msgHelpUpgradeCommand,
        {"vcpkg upgrade --no-dry-run"},
        "https://learn.microsoft.com/vcpkg/commands/upgrade",
        AutocompletePriority::Public,
        0,
        SIZE_MAX,
        {SWITCHES},
        nullptr,
    };

    void command_upgrade_and_exit(const VcpkgCmdArguments& args,
                                  const VcpkgPaths& paths,
                                  Triplet default_triplet,
                                  Triplet host_triplet)
    {
        if (paths.manifest_mode_enabled())
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgUpgradeInManifest);
        }

        const ParsedArguments options = args.parse_arguments(CommandUpgradeMetadata);

        const bool no_dry_run = Util::Sets::contains(options.switches, OPTION_NO_DRY_RUN);
        const KeepGoing keep_going =
            Util::Sets::contains(options.switches, OPTION_NO_KEEP_GOING) ? KeepGoing::NO : KeepGoing::YES;
        const auto unsupported_port_action = Util::Sets::contains(options.switches, OPTION_ALLOW_UNSUPPORTED_PORT)
                                                 ? UnsupportedPortAction::Warn
                                                 : UnsupportedPortAction::Error;

        StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());

        // Load ports from ports dirs
        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(
            fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        ActionPlan action_plan;
        if (options.command_arguments.empty())
        {
            // If no packages specified, upgrade all outdated packages.
            auto outdated_packages = find_outdated_packages(provider, status_db);

            if (outdated_packages.empty())
            {
                msg::println(msgAllPackagesAreUpdated);
                Checks::exit_success(VCPKG_LINE_INFO);
            }

            action_plan = create_upgrade_plan(
                provider,
                var_provider,
                Util::fmap(outdated_packages, [](const OutdatedPackage& package) { return package.spec; }),
                status_db,
                {host_triplet, paths.packages(), unsupported_port_action});
        }
        else
        {
            // input sanitization
            bool default_triplet_used = false;
            const std::vector<PackageSpec> specs = Util::fmap(options.command_arguments, [&](auto&& arg) {
                return check_and_get_package_spec(arg,
                                                  default_triplet,
                                                  default_triplet_used,
                                                  CommandUpgradeMetadata.get_example_text(),
                                                  paths.get_triplet_db());
            });

            if (default_triplet_used)
            {
                print_default_triplet_warning(args, paths.get_triplet_db());
            }

            std::vector<PackageSpec> not_installed;
            std::vector<PackageSpec> no_control_file;
            std::vector<PackageSpec> to_upgrade;
            std::vector<PackageSpec> up_to_date;

            for (auto&& spec : specs)
            {
                bool skip_version_check = false;
                auto installed_status = status_db.find_installed(spec);
                if (installed_status == status_db.end())
                {
                    not_installed.push_back(spec);
                    skip_version_check = true;
                }

                auto maybe_control_file = provider.get_control_file(spec.name());
                if (!maybe_control_file)
                {
                    no_control_file.push_back(spec);
                    skip_version_check = true;
                }

                if (skip_version_check) continue;

                const auto& control_file = maybe_control_file.value_or_exit(VCPKG_LINE_INFO);
                if (control_file.to_version() == (*installed_status)->package.version)
                {
                    up_to_date.push_back(spec);
                }
                else
                {
                    to_upgrade.push_back(spec);
                }
            }

            Util::sort(not_installed);
            Util::sort(no_control_file);
            Util::sort(up_to_date);
            Util::sort(to_upgrade);

            if (!up_to_date.empty())
            {
                msg::println(Color::success, msgFollowingPackagesUpgraded);
                for (const PackageSpec& spec : up_to_date)
                {
                    msg::println(Color::none, LocalizedString().append_indent().append_raw(spec.to_string()));
                }
            }

            if (!not_installed.empty())
            {
                msg::println_error(msgFollowingPackagesNotInstalled);
                for (const PackageSpec& spec : not_installed)
                {
                    msg::println(Color::none, LocalizedString().append_indent().append_raw(spec.to_string()));
                }
            }

            if (!no_control_file.empty())
            {
                msg::println_error(msgFollowingPackagesMissingControl);
                for (const PackageSpec& spec : no_control_file)
                {
                    msg::println(Color::none, LocalizedString().append_indent().append_raw(spec.to_string()));
                }
            }

            Checks::check_exit(VCPKG_LINE_INFO, not_installed.empty() && no_control_file.empty());

            if (to_upgrade.empty()) Checks::exit_success(VCPKG_LINE_INFO);

            action_plan = create_upgrade_plan(provider,
                                              var_provider,
                                              to_upgrade,
                                              status_db,
                                              {host_triplet, paths.packages(), unsupported_port_action});
        }

        Checks::check_exit(VCPKG_LINE_INFO, !action_plan.empty());
        action_plan.print_unsupported_warnings();
        // Set build settings for all install actions
        for (auto&& action : action_plan.install_actions)
        {
            action.build_options = default_build_package_options;
        }

        print_plan(action_plan, true, paths.builtin_ports_directory());

        if (!no_dry_run)
        {
            msg::println(Color::warning, msgUpgradeRunWithNoDryRun);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        var_provider.load_tag_vars(action_plan, host_triplet);

        auto binary_cache = BinaryCache::make(args, paths, out_sink).value_or_exit(VCPKG_LINE_INFO);
        compute_all_abis(paths, action_plan, var_provider, status_db);
        binary_cache.fetch(action_plan.install_actions);
        const InstallSummary summary = install_execute_plan(
            args, action_plan, keep_going, paths, status_db, binary_cache, null_build_logs_recorder());

        if (keep_going == KeepGoing::YES)
        {
            summary.print();
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
