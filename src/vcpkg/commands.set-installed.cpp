#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/commands.remove.h>
#include <vcpkg/commands.set-installed.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/input.h>
#include <vcpkg/metrics.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
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
    static constexpr StringLiteral OPTION_ENFORCE_PORT_CHECKS = "enforce-port-checks";
    static constexpr StringLiteral OPTION_ALLOW_UNSUPPORTED_PORT = "allow-unsupported";

    static constexpr CommandSwitch INSTALL_SWITCHES[] = {
        {OPTION_DRY_RUN, []() { return msg::format(msgCmdSetInstalledOptDryRun); }},
        {OPTION_NO_PRINT_USAGE, []() { return msg::format(msgCmdSetInstalledOptNoUsage); }},
        {OPTION_ONLY_DOWNLOADS, []() { return msg::format(msgHelpTxtOptOnlyDownloads); }},
        {OPTION_ENFORCE_PORT_CHECKS, []() { return msg::format(msgHelpTxtOptEnforcePortChecks); }},
        {OPTION_ALLOW_UNSUPPORTED_PORT, []() { return msg::format(msgHelpTxtOptAllowUnsupportedPort); }},
    };
    static constexpr CommandSetting INSTALL_SETTINGS[] = {
        {OPTION_WRITE_PACKAGES_CONFIG, []() { return msg::format(msgCmdSetInstalledOptWritePkgConfig); }},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("x-set-installed <package>..."); },
        0,
        SIZE_MAX,
        {INSTALL_SWITCHES, INSTALL_SETTINGS},
        nullptr,
    };

    static void send_snapshot_to_api(const VcpkgCmdArguments& args, Json::Object snapshot)
    {
        static constexpr StringLiteral guid_marker = "fcfad8a3-bb68-4a54-ad00-dab1ff671ed2";

        Command cmd;
        cmd.string_arg("curl");
        cmd.string_arg("-w").string_arg("\\n" + guid_marker.to_string() + "%{http_code}");
        cmd.string_arg("-X").string_arg("POST");
        cmd.string_arg("-H").string_arg("Accept: application/vnd.github+json");

        std::string res = "Authorization: Bearer " + *args.github_token.get();
        cmd.string_arg("-H").string_arg(res);
        cmd.string_arg("-H").string_arg("X-GitHub-Api-Version: 2022-11-28");
        cmd.string_arg(Strings::concat(
            "https://api.github.com/repos/", *args.github_repository.get(), "/dependency-graph/snapshots"));
        cmd.string_arg("-d").string_arg(Json::stringify(snapshot));

        int code = 0;
        auto result = cmd_execute_and_stream_lines(cmd, [&code](StringView line) {
            if (Strings::starts_with(line, guid_marker))
            {
                code = std::strtol(line.data() + guid_marker.size(), nullptr, 10);
            }
            else
            {
                Debug::println(line);
            }
        });

        if (auto pres = result.get())
        {
            MetricsSubmission submission;
            if (*pres != 0 || (code >= 100 && code < 200) || code >= 300)
            {
                submission.track_bool(BoolMetric::DependencyGraphSuccess, false);
            }
            else
            {
                submission.track_bool(BoolMetric::DependencyGraphSuccess, true);
            }
            get_global_metrics_collector().track_submission(std::move(submission));
        }
    }

    Json::Object create_dependency_graph_snapshot(const VcpkgCmdArguments& args,
                                                  const ActionPlan& action_plan,
                                                  Optional<std::string> manifest_path)
    {
        auto gh_ref = args.github_ref.value_or_exit(VCPKG_LINE_INFO);
        auto gh_sha = args.github_sha.value_or_exit(VCPKG_LINE_INFO);
        auto gh_repo = args.github_repository.value_or_exit(VCPKG_LINE_INFO);
        auto gh_token = args.github_token.value_or_exit(VCPKG_LINE_INFO);
        auto gh_job_id = args.github_job.value_or_exit(VCPKG_LINE_INFO);
        auto gh_workflow = args.github_workflow.value_or_exit(VCPKG_LINE_INFO);
        auto gh_run_id = args.github_run_id.value_or_exit(VCPKG_LINE_INFO);

        Json::Object detector;
        detector.insert("name", Json::Value::string("vcpkg"));
        detector.insert("url", Json::Value::string(Strings::concat("https://github.com/microsoft/vcpkg")));
        detector.insert("version", Json::Value::string("1.0.0"));

        Json::Object job;
        job.insert("id", Json::Value::string(gh_run_id));
        job.insert("correlator", Json::Value::string(gh_workflow + "-" + gh_job_id));

        Json::Object snapshot;
        snapshot.insert("job", job);
        if (auto v = args.dependency_graph_version.get())
        {
            snapshot.insert("version", Json::Value::integer(std::stoi(*v)));
        }
        else
        {
            snapshot.insert("version", Json::Value::integer(0));
        }

        snapshot.insert("sha", Json::Value::string(gh_sha));
        snapshot.insert("ref", Json::Value::string(gh_ref));
        snapshot.insert("scanned", Json::Value::string(CTime::now_string()));
        snapshot.insert("detector", detector);

        Json::Object manifests;
        Json::Object manifest;
        Json::Object resolved;
        Json::Object file;

        if (auto p = manifest_path.get())
        {
            file.insert("source_location", *p);
        }
        manifest.insert("file", file);
        manifest.insert("name", "vcpkg.json");

        std::unordered_map<std::string, std::string> map;
        for (auto&& action : action_plan.install_actions)
        {
            auto version =
                action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).to_version().to_string();
            auto pkg_url = Strings::concat("pkg:github/vcpkg/", action.spec.name(), "@", version);
            map[action.spec.to_string()] = pkg_url;
        }

        for (auto&& action : action_plan.install_actions)
        {
            Json::Object resolved_item;
            if (map.find(action.spec.to_string()) != map.end())
            {
                auto pkg_url = map[action.spec.to_string()];
                resolved_item.insert("package_url", pkg_url);
                resolved_item.insert("relationship", Json::Value::string("direct"));
                Json::Array deps_list;
                for (auto&& dep : action.package_dependencies)
                {
                    if (map.find(dep.to_string()) != map.end())
                    {
                        auto dep_pkg_url = map[dep.to_string()];
                        deps_list.push_back(dep_pkg_url);
                    }
                }
                resolved_item.insert("dependencies", deps_list);
                resolved.insert(pkg_url, resolved_item);
            }
        }
        manifest.insert("resolved", resolved);
        manifests.insert("vcpkg.json", manifest);
        snapshot.insert("manifests", manifests);

        Debug::print(Json::stringify(snapshot));
        return snapshot;
    }

    std::set<PackageSpec> adjust_action_plan_to_status_db(ActionPlan& action_plan, const StatusParagraphs& status_db)
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
        action_plan.remove_actions = create_remove_plan(specs_to_remove, status_db).remove;

        for (const auto& action : action_plan.remove_actions)
        {
            // This should not technically be needed, however ensuring that all specs to be removed are not included in
            // `specs_installed` acts as a sanity check
            specs_installed.erase(action.spec);
        }

        Util::erase_remove_if(action_plan.install_actions, [&](const InstallPlanAction& ipa) {
            return Util::Sets::contains(specs_installed, ipa.spec);
        });
        return specs_installed;
    }

    void perform_and_exit_ex(const VcpkgCmdArguments& args,
                             const VcpkgPaths& paths,
                             const PathsPortFileProvider& provider,
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

        if (paths.manifest_mode_enabled() && paths.get_feature_flags().dependency_graph)
        {
            auto gh_root_dir = args.github_workspace.value_or_exit(VCPKG_LINE_INFO);
            auto snapshot =
                create_dependency_graph_snapshot(args, action_plan, paths.relative_path_to_manifest(gh_root_dir));
            send_snapshot_to_api(args, snapshot);
        }

        // currently (or once) installed specifications
        auto status_db = database_load_check(fs, paths.installed());
        adjust_action_plan_to_status_db(action_plan, status_db);

        print_plan(action_plan, true, paths.builtin_ports_directory());

        if (auto p_pkgsconfig = maybe_pkgsconfig.get())
        {
            auto pkgsconfig_path = paths.original_cwd / *p_pkgsconfig;
            auto pkgsconfig_contents = generate_nuget_packages_config(action_plan, args.nuget_id_prefix.value_or(""));
            fs.write_contents(pkgsconfig_path, pkgsconfig_contents, VCPKG_LINE_INFO);
            msg::println(msgWroteNuGetPkgConfInfo, msg::path = pkgsconfig_path);
        }

        if (dry_run == DryRun::Yes)
        {
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        paths.flush_lockfile();

        track_install_plan(action_plan);
        Install::preclear_packages(paths, action_plan);

        auto binary_cache = only_downloads ? BinaryCache(paths.get_filesystem())
                                           : BinaryCache::make(args, paths, stdout_sink).value_or_exit(VCPKG_LINE_INFO);
        binary_cache.fetch(action_plan.install_actions);
        const auto summary = Install::execute_plan(
            args, action_plan, keep_going, paths, status_db, binary_cache, null_build_logs_recorder());

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
        bool default_triplet_used = false;
        const std::vector<FullPackageSpec> specs = Util::fmap(options.command_arguments, [&](auto&& arg) {
            return check_and_get_full_package_spec(
                arg, default_triplet, default_triplet_used, COMMAND_STRUCTURE.get_example_text(), paths);
        });

        if (default_triplet_used)
        {
            print_default_triplet_warning(args);
        }

        const bool dry_run = Util::Sets::contains(options.switches, OPTION_DRY_RUN);
        const bool only_downloads = Util::Sets::contains(options.switches, OPTION_ONLY_DOWNLOADS);
        const KeepGoing keep_going = Util::Sets::contains(options.switches, OPTION_KEEP_GOING) || only_downloads
                                         ? KeepGoing::YES
                                         : KeepGoing::NO;
        const PrintUsage print_cmake_usage =
            Util::Sets::contains(options.switches, OPTION_NO_PRINT_USAGE) ? PrintUsage::NO : PrintUsage::YES;
        const auto unsupported_port_action = Util::Sets::contains(options.switches, OPTION_ALLOW_UNSUPPORTED_PORT)
                                                 ? UnsupportedPortAction::Warn
                                                 : UnsupportedPortAction::Error;
        const bool prohibit_backcompat_features = Util::Sets::contains(options.switches, (OPTION_ENFORCE_PORT_CHECKS));

        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(
            fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));
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
        auto action_plan = create_feature_install_plan(
            provider, *cmake_vars, specs, {}, {host_triplet, paths.packages(), unsupported_port_action});

        for (auto&& action : action_plan.install_actions)
        {
            action.build_options = default_build_package_options;
            action.build_options.backcompat_features =
                (prohibit_backcompat_features ? BackcompatFeatures::PROHIBIT : BackcompatFeatures::ALLOW);
        }

        perform_and_exit_ex(args,
                            paths,
                            provider,
                            *cmake_vars,
                            std::move(action_plan),
                            dry_run ? DryRun::Yes : DryRun::No,
                            pkgsconfig,
                            host_triplet,
                            keep_going,
                            only_downloads,
                            print_cmake_usage);
    }
}
