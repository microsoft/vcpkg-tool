#include <vcpkg/base/messages.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.upgrade.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/install.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/update.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace vcpkg::Commands::Upgrade
{
    static constexpr StringLiteral OPTION_NO_DRY_RUN = "no-dry-run";
    // --keep-going is preserved for compatibility with old releases of vcpkg.
    static constexpr StringLiteral OPTION_KEEP_GOING = "keep-going";
    static constexpr StringLiteral OPTION_NO_KEEP_GOING = "no-keep-going";
    static constexpr StringLiteral OPTION_ALLOW_UNSUPPORTED_PORT = "allow-unsupported";

    static constexpr std::array<CommandSwitch, 4> INSTALL_SWITCHES = {{
        {OPTION_NO_DRY_RUN, []() { return msg::format(msgCmdUpgradeOptNoDryRun); }},
        {OPTION_KEEP_GOING, nullptr},
        {OPTION_NO_KEEP_GOING, []() { return msg::format(msgCmdUpgradeOptNoKeepGoing); }},
        {OPTION_ALLOW_UNSUPPORTED_PORT, []() { return msg::format(msgCmdUpgradeOptAllowUnsupported); }},
    }};

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("upgrade --no-dry-run"),
        0,
        SIZE_MAX,
        {INSTALL_SWITCHES, {}},
        nullptr,
    };

    static KeepGoing determine_keep_going(bool keep_going_set, bool no_keep_going_set)
    {
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               !(keep_going_set && no_keep_going_set),
                               msg::msgBothYesAndNoOptionSpecifiedError,
                               msg::option = OPTION_KEEP_GOING);
        if (keep_going_set)
        {
            return KeepGoing::YES;
        }

        if (no_keep_going_set)
        {
            return KeepGoing::NO;
        }

        return KeepGoing::YES;
    }

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet)
    {
        if (paths.manifest_mode_enabled())
        {
            msg::println_error(msgUpgradeInManifest);
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        const bool no_dry_run = Util::Sets::contains(options.switches, OPTION_NO_DRY_RUN);
        const KeepGoing keep_going = determine_keep_going(Util::Sets::contains(options.switches, OPTION_KEEP_GOING),
                                                          Util::Sets::contains(options.switches, OPTION_NO_KEEP_GOING));
        const auto unsupported_port_action = Util::Sets::contains(options.switches, OPTION_ALLOW_UNSUPPORTED_PORT)
                                                 ? UnsupportedPortAction::Warn
                                                 : UnsupportedPortAction::Error;

        BinaryCache binary_cache{args, paths};
        StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());

        // Load ports from ports dirs
        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(
            fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        // input sanitization
        const std::vector<PackageSpec> specs = Util::fmap(args.command_arguments, [&](auto&& arg) {
            return check_and_get_package_spec(std::string(arg), default_triplet, COMMAND_STRUCTURE.example_text, paths);
        });

        ActionPlan action_plan;
        if (specs.empty())
        {
            // If no packages specified, upgrade all outdated packages.
            auto outdated_packages = Update::find_outdated_packages(provider, status_db);

            if (outdated_packages.empty())
            {
                msg::println(msgAllPackagesAreUpdated);
                Checks::exit_success(VCPKG_LINE_INFO);
            }

            action_plan = create_upgrade_plan(
                provider,
                var_provider,
                Util::fmap(outdated_packages, [](const Update::OutdatedPackage& package) { return package.spec; }),
                status_db,
                {host_triplet, unsupported_port_action});
        }
        else
        {
            print_default_triplet_warning(args, args.command_arguments);

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
                const auto& control_paragraph = *control_file.source_control_file->core_paragraph;
                auto control_version = Version(control_paragraph.raw_version, control_paragraph.port_version);
                const auto& installed_paragraph = (*installed_status)->package;
                auto installed_version = Version(installed_paragraph.version, installed_paragraph.port_version);
                if (control_version == installed_version)
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

            action_plan = create_upgrade_plan(
                provider, var_provider, to_upgrade, status_db, {host_triplet, unsupported_port_action});
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

        var_provider.load_tag_vars(action_plan, provider, host_triplet);

        const InstallSummary summary = Install::perform(
            args, action_plan, keep_going, paths, status_db, binary_cache, null_build_logs_recorder(), var_provider);

        if (keep_going == KeepGoing::YES)
        {
            summary.print();
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void UpgradeCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                          const VcpkgPaths& paths,
                                          Triplet default_triplet,
                                          Triplet host_triplet) const
    {
        Upgrade::perform_and_exit(args, paths, default_triplet, host_triplet);
    }
}
