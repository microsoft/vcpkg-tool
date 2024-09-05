#include <vcpkg/base/contractual-constants.h>
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
    constexpr CommandSwitch SWITCHES[] = {
        {SwitchNoDryRun, msgCmdUpgradeOptNoDryRun},
        {SwitchNoKeepGoing, msgCmdUpgradeOptNoKeepGoing},
        {SwitchAllowUnsupported, msgHelpTxtOptAllowUnsupportedPort},
    };

    void print_versioned_package_spec_list(View<VersionedPackageSpec> specs)
    {
        for (const VersionedPackageSpec& spec : specs)
        {
            msg::println(Color::none, LocalizedString().append_indent().append_raw(spec.to_string()));
        }
    }
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

        const bool no_dry_run = Util::Sets::contains(options.switches, SwitchNoDryRun);
        const KeepGoing keep_going =
            Util::Sets::contains(options.switches, SwitchNoKeepGoing) ? KeepGoing::No : KeepGoing::Yes;
        const auto unsupported_port_action = Util::Sets::contains(options.switches, SwitchAllowUnsupported)
                                                 ? UnsupportedPortAction::Warn
                                                 : UnsupportedPortAction::Error;

        static const BuildPackageOptions build_options{
            BuildMissing::Yes,
            AllowDownloads::Yes,
            OnlyDownloads::No,
            CleanBuildtrees::Yes,
            CleanPackages::Yes,
            CleanDownloads::No,
            DownloadTool::Builtin,
            BackcompatFeatures::Allow,
            PrintUsage::Yes,
            keep_going,
        };

        const CreateUpgradePlanOptions create_upgrade_plan_options{
            nullptr, host_triplet, paths.packages(), unsupported_port_action};

        StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());

        // Load ports from ports dirs
        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set,
                                       make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        ActionPlan action_plan;
        std::vector<PackageSpec> not_installed;
        std::vector<VersionedPackageSpec> attempt_upgrade_specs;
        if (options.command_arguments.empty())
        {
            // If no packages specified, upgrade all outdated packages.
            attempt_upgrade_specs = get_installed_port_version_specs(status_db);
        }
        else
        {
            auto installed_ports = get_installed_ports(status_db);

            // input sanitization
            const std::vector<PackageSpec> specs = Util::fmap(options.command_arguments, [&](const std::string& arg) {
                return check_and_get_package_spec(arg, default_triplet, paths.get_triplet_db())
                    .value_or_exit(VCPKG_LINE_INFO);
            });

            for (auto&& requested_spec : specs)
            {
                auto installed = installed_ports.find(requested_spec);
                if (installed == installed_ports.end())
                {
                    not_installed.push_back(requested_spec);
                }
                else
                {
                    attempt_upgrade_specs.push_back(VersionedPackageSpec{
                        requested_spec.name(), requested_spec.triplet(), installed->second.version()});
                }
            }
        }

        auto outdated_report = build_outdated_report(provider, attempt_upgrade_specs);
        if (options.command_arguments.empty())
        {
            if (!outdated_report.parse_errors.empty())
            {
                if (keep_going == KeepGoing::No)
                {
                    msg::println_error(msgUpgradeParseError);
                }
                else
                {
                    msg::println_warning(msgUpgradeParseWarning);
                }

                for (auto&& parse_error : outdated_report.parse_errors)
                {
                    msg::println(parse_error);
                }

                if (keep_going == KeepGoing::No)
                {
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }
            }
        }
        else if (!outdated_report.parse_errors.empty())
        {
            for (auto&& parse_error : outdated_report.parse_errors)
            {
                msg::println(parse_error);
            }

            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        if (!outdated_report.up_to_date_packages.empty())
        {
            msg::println(Color::success, msgFollowingPackagesUpgraded);
            print_versioned_package_spec_list(outdated_report.up_to_date_packages);
        }

        if (!outdated_report.missing_packages.empty())
        {
            msg::println(Color::none, msgUpgradeInstalledMissing);
            print_versioned_package_spec_list(outdated_report.missing_packages);
        }

        if (!not_installed.empty())
        {
            msg::println_error(msgFollowingPackagesNotInstalled);
            for (const PackageSpec& spec : not_installed)
            {
                msg::println(Color::none, LocalizedString().append_indent().append_raw(spec.to_string()));
            }

            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        if (outdated_report.outdated_packages.empty())
        {
            msg::println(msgAllPackagesAreUpdated);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        action_plan = create_upgrade_plan(
            provider,
            var_provider,
            Util::fmap(outdated_report.outdated_packages, [](const OutdatedPackage& package) { return package.spec; }),
            status_db,
            create_upgrade_plan_options);

        Checks::check_exit(VCPKG_LINE_INFO, !action_plan.empty());
        action_plan.print_unsupported_warnings();
        print_plan(action_plan, paths.builtin_ports_directory());

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
            args, paths, host_triplet, build_options, action_plan, status_db, binary_cache, null_build_logs_recorder());

        // Skip printing the summary without --keep-going because the status without it is 'obvious': everything was a
        // success.
        if (keep_going == KeepGoing::Yes)
        {
            msg::print(summary.format());
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
