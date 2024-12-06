#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/commands.set-installed.h>
#include <vcpkg/input.h>
#include <vcpkg/metrics.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    constexpr CommandSwitch INSTALL_SWITCHES[] = {
        {SwitchDryRun, msgCmdSetInstalledOptDryRun},
        {SwitchNoPrintUsage, msgCmdSetInstalledOptNoUsage},
        {SwitchOnlyDownloads, msgHelpTxtOptOnlyDownloads},
        {SwitchKeepGoing, msgHelpTxtOptKeepGoing},
        {SwitchEnforcePortChecks, msgHelpTxtOptEnforcePortChecks},
        {SwitchAllowUnsupported, msgHelpTxtOptAllowUnsupportedPort},
    };

    constexpr CommandSetting INSTALL_SETTINGS[] = {
        {SwitchXWriteNuGetPackagesConfig, msgCmdSetInstalledOptWritePkgConfig},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandSetInstalledMetadata = {
        "x-set-installed",
        msgCmdSetInstalledSynopsis,
        {msgCmdSetInstalledExample1, "vcpkg x-set-installed zlib:x64-windows boost"},
        Undocumented,
        AutocompletePriority::Public,
        0,
        SIZE_MAX,
        {INSTALL_SWITCHES, INSTALL_SETTINGS},
        nullptr,
    };

    Optional<Json::Object> create_dependency_graph_snapshot(const VcpkgCmdArguments& args,
                                                            const ActionPlan& action_plan)
    {
        const auto github_ref = args.github_ref.get();
        const auto github_sha = args.github_sha.get();
        const auto github_job = args.github_job.get();
        const auto github_workflow = args.github_workflow.get();
        const auto github_run_id = args.github_run_id.get();
        if (github_ref && github_sha && github_job && github_workflow && github_run_id)
        {
            Json::Object detector;
            detector.insert(JsonIdName, Json::Value::string("vcpkg"));
            detector.insert(JsonIdUrl, Json::Value::string("https://github.com/microsoft/vcpkg"));
            detector.insert(JsonIdVersion, Json::Value::string("1.0.0"));

            Json::Object job;
            job.insert(JsonIdId, Json::Value::string(*github_run_id));
            job.insert(JsonIdCorrelator, Json::Value::string(fmt::format("{}-{}", *github_workflow, *github_run_id)));

            Json::Object snapshot;
            snapshot.insert(JsonIdJob, job);
            snapshot.insert(JsonIdVersion, Json::Value::integer(0));
            snapshot.insert(JsonIdSha, Json::Value::string(*github_sha));
            snapshot.insert(JsonIdRef, Json::Value::string(*github_ref));
            snapshot.insert(JsonIdScanned, Json::Value::string(CTime::now_string()));
            snapshot.insert(JsonIdDetector, detector);

            Json::Object manifest;
            manifest.insert(JsonIdName, FileVcpkgDotJson);

            std::unordered_map<std::string, std::string> map;
            for (auto&& action : action_plan.install_actions)
            {
                const auto scfl = action.source_control_file_and_location.get();
                if (!scfl)
                {
                    return nullopt;
                }
                auto spec = action.spec.to_string();
                map.insert(
                    {spec, fmt::format("pkg:github/vcpkg/{}@{}", spec, scfl->source_control_file->to_version())});
            }

            Json::Object resolved;
            for (auto&& action : action_plan.install_actions)
            {
                Json::Object resolved_item;
                auto spec = action.spec.to_string();
                const auto found = map.find(spec);
                if (found != map.end())
                {
                    const auto& pkg_url = found->second;
                    resolved_item.insert(JsonIdPackageUnderscoreUrl, pkg_url);
                    resolved_item.insert(JsonIdRelationship, Json::Value::string(JsonIdDirect));
                    Json::Array deps_list;
                    for (auto&& dep : action.package_dependencies)
                    {
                        const auto found_dep = map.find(dep.to_string());
                        if (found_dep != map.end())
                        {
                            deps_list.push_back(found_dep->second);
                        }
                    }
                    resolved_item.insert(JsonIdDependencies, deps_list);
                    resolved.insert(pkg_url, resolved_item);
                }
            }

            manifest.insert(JsonIdResolved, resolved);
            Json::Object manifests;
            manifests.insert(JsonIdVcpkgDotJson, manifest);
            snapshot.insert(JsonIdManifests, manifests);
            Debug::print(Json::stringify(snapshot));
            return snapshot;
        }
        return nullopt;
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

    void command_set_installed_and_exit_ex(const VcpkgCmdArguments& args,
                                           const VcpkgPaths& paths,
                                           Triplet host_triplet,
                                           const BuildPackageOptions& build_options,
                                           const CMakeVars::CMakeVarProvider& cmake_vars,
                                           ActionPlan action_plan,
                                           DryRun dry_run,
                                           PrintUsage print_usage,
                                           const Optional<Path>& maybe_pkgconfig,
                                           bool include_manifest_in_github_issue)
    {
        auto& fs = paths.get_filesystem();

        cmake_vars.load_tag_vars(action_plan, host_triplet);
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
            msg::println(msgDependencyGraphCalculation);
            auto snapshot = create_dependency_graph_snapshot(args, action_plan);
            bool s = false;
            if (snapshot.has_value() && args.github_token.has_value() && args.github_repository.has_value())
            {
                s = send_snapshot_to_api(*args.github_token.get(), *args.github_repository.get(), *snapshot.get());
                if (s)
                {
                    msg::println(msgDependencyGraphSuccess);
                }
                else
                {
                    msg::println(msgDependencyGraphFailure);
                }
            }
            get_global_metrics_collector().track_bool(BoolMetric::DependencyGraphSuccess, s);
        }

        // currently (or once) installed specifications
        auto status_db = database_load_check(fs, paths.installed());
        adjust_action_plan_to_status_db(action_plan, status_db);

        print_plan(action_plan, paths.builtin_ports_directory());

        if (auto p_pkgsconfig = maybe_pkgconfig.get())
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
        install_preclear_packages(paths, action_plan);

        auto binary_cache = build_options.only_downloads == OnlyDownloads::Yes
                                ? BinaryCache(paths.get_filesystem())
                                : BinaryCache::make(args, paths, out_sink).value_or_exit(VCPKG_LINE_INFO);
        binary_cache.fetch(action_plan.install_actions);
        const auto summary = install_execute_plan(args,
                                                  paths,
                                                  host_triplet,
                                                  build_options,
                                                  action_plan,
                                                  status_db,
                                                  binary_cache,
                                                  null_build_logs_recorder(),
                                                  include_manifest_in_github_issue);

        if (build_options.keep_going == KeepGoing::Yes && summary.failed())
        {
            summary.print_failed();
            if (build_options.only_downloads == OnlyDownloads::No)
            {
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        if (print_usage == PrintUsage::Yes)
        {
            // Note that this differs from the behavior of `vcpkg install` in that it will print usage information for
            // packages named but not installed here
            std::set<std::string> printed_usages;
            for (auto&& ur_spec : user_requested_specs)
            {
                auto it = status_db.find_installed(ur_spec);
                if (it != status_db.end())
                {
                    install_print_usage_information(it->get()->package, printed_usages, fs, paths.installed());
                }
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void command_set_installed_and_exit(const VcpkgCmdArguments& args,
                                        const VcpkgPaths& paths,
                                        Triplet default_triplet,
                                        Triplet host_triplet)
    {
        // input sanitization
        const ParsedArguments options = args.parse_arguments(CommandSetInstalledMetadata);
        const std::vector<FullPackageSpec> specs = Util::fmap(options.command_arguments, [&](const std::string& arg) {
            return check_and_get_full_package_spec(arg, default_triplet, paths.get_triplet_db())
                .value_or_exit(VCPKG_LINE_INFO);
        });

        const auto only_downloads =
            Util::Sets::contains(options.switches, SwitchOnlyDownloads) ? OnlyDownloads::Yes : OnlyDownloads::No;
        const auto keep_going =
            Util::Sets::contains(options.switches, SwitchKeepGoing) || only_downloads == OnlyDownloads::Yes
                ? KeepGoing::Yes
                : KeepGoing::No;
        const auto unsupported_port_action = Util::Sets::contains(options.switches, SwitchAllowUnsupported)
                                                 ? UnsupportedPortAction::Warn
                                                 : UnsupportedPortAction::Error;
        const auto prohibit_backcompat_features = Util::Sets::contains(options.switches, SwitchEnforcePortChecks)
                                                      ? BackcompatFeatures::Prohibit
                                                      : BackcompatFeatures::Allow;

        const BuildPackageOptions build_options{
            BuildMissing::Yes,
            AllowDownloads::Yes,
            only_downloads,
            CleanBuildtrees::Yes,
            CleanPackages::Yes,
            CleanDownloads::No,
            DownloadTool::Builtin,
            prohibit_backcompat_features,
            keep_going,
        };

        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set, make_overlay_provider(fs, paths.overlay_ports));
        auto cmake_vars = CMakeVars::make_triplet_cmake_var_provider(paths);

        Optional<Path> pkgsconfig;
        auto it_pkgsconfig = options.settings.find(SwitchXWriteNuGetPackagesConfig);
        if (it_pkgsconfig != options.settings.end())
        {
            get_global_metrics_collector().track_define(DefineMetric::X_WriteNugetPackagesConfig);
            pkgsconfig = it_pkgsconfig->second;
        }

        // We have a set of user-requested specs.
        // We need to know all the specs which are required to fulfill dependencies for those specs.
        // Therefore, we see what we would install into an empty installed tree, so we can use the existing code.
        auto action_plan = create_feature_install_plan(
            provider,
            *cmake_vars,
            specs,
            {},
            {nullptr, host_triplet, paths.packages(), unsupported_port_action, UseHeadVersion::No, Editable::No});

        command_set_installed_and_exit_ex(
            args,
            paths,
            host_triplet,
            build_options,
            *cmake_vars,
            std::move(action_plan),
            Util::Sets::contains(options.switches, SwitchDryRun) ? DryRun::Yes : DryRun::No,
            Util::Sets::contains(options.switches, SwitchNoPrintUsage) ? PrintUsage::No : PrintUsage::Yes,
            pkgsconfig,
            false);
    }
} // namespace vcpkg
