#include <vcpkg/base/system.print.h>
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

namespace vcpkg::Commands::Upgrade
{
    using Install::KeepGoing;
    using Install::to_keep_going;

    static constexpr StringLiteral OPTION_NO_DRY_RUN = "no-dry-run";
    static constexpr StringLiteral OPTION_KEEP_GOING = "keep-going";
    static constexpr StringLiteral OPTION_ALLOW_UNSUPPORTED_PORT = "allow-unsupported";

    static constexpr std::array<CommandSwitch, 3> INSTALL_SWITCHES = {{
        {OPTION_NO_DRY_RUN, "Actually upgrade"},
        {OPTION_KEEP_GOING, "Continue installing packages on failure"},
        {OPTION_ALLOW_UNSUPPORTED_PORT, "Instead of erroring on an unsupported port, continue with a warning."},
    }};

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("upgrade --no-dry-run"),
        0,
        SIZE_MAX,
        {INSTALL_SWITCHES, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet)
    {
        if (paths.manifest_mode_enabled())
        {
            Checks::exit_maybe_upgrade(VCPKG_LINE_INFO,
                                       "Error: the upgrade command does not currently support manifest mode. Instead, "
                                       "modify your vcpkg.json and run install.");
        }

        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        const bool no_dry_run = Util::Sets::contains(options.switches, OPTION_NO_DRY_RUN);
        const KeepGoing keep_going = to_keep_going(Util::Sets::contains(options.switches, OPTION_KEEP_GOING));
        const auto unsupported_port_action = Util::Sets::contains(options.switches, OPTION_ALLOW_UNSUPPORTED_PORT)
                                                 ? Dependencies::UnsupportedPortAction::Warn
                                                 : Dependencies::UnsupportedPortAction::Error;

        BinaryCache binary_cache{args};
        StatusParagraphs status_db = database_load_check(paths);

        // Load ports from ports dirs
        PortFileProvider::PathsPortFileProvider provider(paths, args.overlay_ports);
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        // input sanitization
        const std::vector<PackageSpec> specs = Util::fmap(args.command_arguments, [&](auto&& arg) {
            return Input::check_and_get_package_spec(std::string(arg), default_triplet, COMMAND_STRUCTURE.example_text);
        });

        for (auto&& spec : specs)
        {
            Input::check_triplet(spec.triplet(), paths);
        }

        Dependencies::ActionPlan action_plan;
        if (specs.empty())
        {
            // If no packages specified, upgrade all outdated packages.
            auto outdated_packages = Update::find_outdated_packages(provider, status_db);

            if (outdated_packages.empty())
            {
                print2("All installed packages are up-to-date with the local portfiles.\n");
                Checks::exit_success(VCPKG_LINE_INFO);
            }

            action_plan = Dependencies::create_upgrade_plan(
                provider,
                var_provider,
                Util::fmap(outdated_packages, [](const Update::OutdatedPackage& package) { return package.spec; }),
                status_db,
                {host_triplet, unsupported_port_action});
        }
        else
        {
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
                if (!maybe_control_file.has_value())
                {
                    no_control_file.push_back(spec);
                    skip_version_check = true;
                }

                if (skip_version_check) continue;

                const auto& control_file = maybe_control_file.value_or_exit(VCPKG_LINE_INFO);
                const auto& control_paragraph = *control_file.source_control_file->core_paragraph;
                auto control_version = VersionT(control_paragraph.version, control_paragraph.port_version);
                const auto& installed_paragraph = (*installed_status)->package;
                auto installed_version = VersionT(installed_paragraph.version, installed_paragraph.port_version);
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
                print2(Color::success, "The following packages are up-to-date:\n");
                print2(Strings::join(
                           "", up_to_date, [](const PackageSpec& spec) { return "    " + spec.to_string() + "\n"; }),
                       '\n');
            }

            if (!not_installed.empty())
            {
                print2(Color::error, "The following packages are not installed:\n");
                print2(Strings::join(
                           "", not_installed, [](const PackageSpec& spec) { return "    " + spec.to_string() + "\n"; }),
                       '\n');
            }

            if (!no_control_file.empty())
            {
                print2(Color::error, "The following packages do not have a valid CONTROL or vcpkg.json:\n");
                print2(Strings::join("",
                                     no_control_file,
                                     [](const PackageSpec& spec) { return "    " + spec.to_string() + "\n"; }),
                       '\n');
            }

            Checks::check_exit(VCPKG_LINE_INFO, not_installed.empty() && no_control_file.empty());

            if (to_upgrade.empty()) Checks::exit_success(VCPKG_LINE_INFO);

            action_plan = Dependencies::create_upgrade_plan(
                provider, var_provider, to_upgrade, status_db, {host_triplet, unsupported_port_action});
        }

        Checks::check_exit(VCPKG_LINE_INFO, !action_plan.empty());
        for (const auto& warning : action_plan.warnings)
        {
            print2(Color::warning, warning, '\n');
        }
        // Set build settings for all install actions
        for (auto&& action : action_plan.install_actions)
        {
            action.build_options = vcpkg::Build::default_build_package_options;
        }

        Dependencies::print_plan(action_plan, true, paths.builtin_ports_directory());

        if (!no_dry_run)
        {
            print2(Color::warning,
                   "If you are sure you want to rebuild the above packages, run this command with the "
                   "--no-dry-run option.\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        var_provider.load_tag_vars(action_plan, provider, host_triplet);

        const Install::InstallSummary summary = Install::perform(args,
                                                                 action_plan,
                                                                 keep_going,
                                                                 paths,
                                                                 status_db,
                                                                 binary_cache,
                                                                 Build::null_build_logs_recorder(),
                                                                 var_provider);

        print2("\nTotal elapsed time: ", summary.total_elapsed_time, "\n\n");

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
