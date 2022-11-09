#include <vcpkg/base/system.print.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/commands.setinstalled.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/install.h>
#include <vcpkg/metrics.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/remove.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands::SetInstalled
{
    static constexpr StringLiteral OPTION_DRY_RUN = "dry-run";
    static constexpr StringLiteral OPTION_KEEP_GOING = "keep-going";
    static constexpr StringLiteral OPTION_ONLY_DOWNLOADS = "only-downloads";
    static constexpr StringLiteral OPTION_WRITE_PACKAGES_CONFIG = "x-write-nuget-packages-config";
    static constexpr StringLiteral OPTION_NO_PRINT_USAGE = "no-print-usage";

    static constexpr CommandSwitch INSTALL_SWITCHES[] = {
        {OPTION_DRY_RUN, "Do not actually build or install"},
        {OPTION_NO_PRINT_USAGE, "Don't print cmake usage information after install."}};
    static constexpr CommandSetting INSTALL_SETTINGS[] = {
        {OPTION_WRITE_PACKAGES_CONFIG,
         "Writes out a NuGet packages.config-formatted file for use with external binary caching.\n"
         "See `vcpkg help binarycaching` for more information."},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string(R"(x-set-installed <package>...)"),
        0,
        SIZE_MAX,
        {INSTALL_SWITCHES, INSTALL_SETTINGS},
        nullptr,
    };

    void adjust_action_plan_to_status_db(ActionPlan& action_plan, const StatusParagraphs& status_db)
    {
        std::set<std::string> all_abis;
        for (const auto& action : action_plan.install_actions)
        {
            all_abis.insert(action.abi_info.value_or_exit(VCPKG_LINE_INFO).package_abi);
        }

        std::vector<PackageSpec> specs_to_remove;
        std::set<PackageSpec> specs_installed;
        for (auto&& status_pgh : status_db)
        {
            if (!status_pgh->is_installed()) continue;
            if (status_pgh->package.is_feature()) continue;

            const auto& abi = status_pgh->package.abi;
            if (abi.empty() || !Util::Sets::contains(all_abis, abi))
            {
                specs_to_remove.push_back(status_pgh->package.spec);
            }
            else
            {
                specs_installed.emplace(status_pgh->package.spec);
            }
        }

        action_plan.remove_actions = create_remove_plan(specs_to_remove, status_db);

        for (const auto& action : action_plan.remove_actions)
        {
            // This should not technically be needed, however ensuring that all specs to be removed are not included in
            // `specs_installed` acts as a sanity check
            specs_installed.erase(action.spec);
        }

        Util::erase_remove_if(action_plan.install_actions, [&](const InstallPlanAction& ipa) {
            return Util::Sets::contains(specs_installed, ipa.spec);
        });
    }

    void perform_and_exit_ex(const VcpkgCmdArguments& args,
                             const VcpkgPaths& paths,
                             const PathsPortFileProvider& provider,
                             BinaryCache& binary_cache,
                             const CMakeVars::CMakeVarProvider& cmake_vars,
                             ActionPlan action_plan,
                             DryRun dry_run,
                             const Optional<Path>& maybe_pkgsconfig,
                             Triplet host_triplet,
                             const KeepGoing keep_going,
                             const bool only_downloads,
                             const PrintUsage print_cmake_usage)
    {
        auto& fs = paths.get_filesystem();

        cmake_vars.load_tag_vars(action_plan, provider, host_triplet);
        compute_all_abis(paths, action_plan, cmake_vars, {});

        std::vector<PackageSpec> user_requested_specs;
        for (const auto& action : action_plan.install_actions)
        {
            if (action.request_type == RequestType::USER_REQUESTED)
            {
                // save for reporting usage later
                user_requested_specs.push_back(action.spec);
            }
        }

        // currently (or once) installed specifications
        auto status_db = database_load_check(fs, paths.installed());
        adjust_action_plan_to_status_db(action_plan, status_db);

        print_plan(action_plan, true, paths.builtin_ports_directory());

        if (auto p_pkgsconfig = maybe_pkgsconfig.get())
        {
            compute_all_abis(paths, action_plan, cmake_vars, status_db);
            auto pkgsconfig_path = paths.original_cwd / *p_pkgsconfig;
            auto pkgsconfig_contents = generate_nuget_packages_config(action_plan);
            fs.write_contents(pkgsconfig_path, pkgsconfig_contents, VCPKG_LINE_INFO);
            msg::println(msgWroteNuGetPkgConfInfo, msg::path = pkgsconfig_path);
        }

        if (dry_run == DryRun::Yes)
        {
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        paths.flush_lockfile();

        track_install_plan(action_plan);

        const auto summary = Install::perform(
            args, action_plan, keep_going, paths, status_db, binary_cache, null_build_logs_recorder(), cmake_vars);

        if (keep_going == KeepGoing::YES && summary.failed())
        {
            summary.print_failed();
            if (!only_downloads)
            {
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        if (print_cmake_usage == PrintUsage::YES)
        {
            std::set<std::string> printed_usages;
            for (auto&& ur_spec : user_requested_specs)
            {
                auto it = status_db.find_installed(ur_spec);
                if (it != status_db.end())
                {
                    Install::print_usage_information(it->get()->package, printed_usages, fs, paths.installed());
                }
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet)
    {
        // input sanitization
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        const std::vector<FullPackageSpec> specs = Util::fmap(args.command_arguments, [&](auto&& arg) {
            return check_and_get_full_package_spec(
                std::string(arg), default_triplet, COMMAND_STRUCTURE.example_text, paths);
        });

        BinaryCache binary_cache{args, paths};

        const bool dry_run = Util::Sets::contains(options.switches, OPTION_DRY_RUN);
        const bool only_downloads = Util::Sets::contains(options.switches, OPTION_ONLY_DOWNLOADS);
        const KeepGoing keep_going = Util::Sets::contains(options.switches, OPTION_KEEP_GOING) || only_downloads
                                         ? KeepGoing::YES
                                         : KeepGoing::NO;
        const PrintUsage print_cmake_usage =
            Util::Sets::contains(options.switches, OPTION_NO_PRINT_USAGE) ? PrintUsage::NO : PrintUsage::YES;

        PathsPortFileProvider provider(paths, make_overlay_provider(paths, paths.overlay_ports));
        auto cmake_vars = CMakeVars::make_triplet_cmake_var_provider(paths);

        Optional<Path> pkgsconfig;
        auto it_pkgsconfig = options.settings.find(OPTION_WRITE_PACKAGES_CONFIG);
        if (it_pkgsconfig != options.settings.end())
        {
            get_global_metrics_collector().track_define(DefineMetric::X_WriteNugetPackagesConfig);
            pkgsconfig = it_pkgsconfig->second;
        }

        // We have a set of user-requested specs.
        // We need to know all the specs which are required to fulfill dependencies for those specs.
        // Therefore, we see what we would install into an empty installed tree, so we can use the existing code.
        auto action_plan = create_feature_install_plan(provider, *cmake_vars, specs, {}, {host_triplet});

        for (auto&& action : action_plan.install_actions)
        {
            action.build_options = default_build_package_options;
        }

        perform_and_exit_ex(args,
                            paths,
                            provider,
                            binary_cache,
                            *cmake_vars,
                            std::move(action_plan),
                            dry_run ? DryRun::Yes : DryRun::No,
                            pkgsconfig,
                            host_triplet,
                            keep_going,
                            only_downloads,
                            print_cmake_usage);
    }

    void SetInstalledCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                               const VcpkgPaths& paths,
                                               Triplet default_triplet,
                                               Triplet host_triplet) const
    {
        SetInstalled::perform_and_exit(args, paths, default_triplet, host_triplet);
    }

}
