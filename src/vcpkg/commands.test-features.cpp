#include <vcpkg/base/cache.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/graphs.h>
#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/ci-feature-baseline.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/commands.set-installed.h>
#include <vcpkg/commands.test-features.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/input.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/platform-expression.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    Path ci_build_log_feature_test_base_path(const Path& base_path, std::size_t counter, const FullPackageSpec& spec)
    {
        std::string feature_dir = spec.package_spec.name();
        feature_dir.push_back('_');
        if (spec.features.size() == 1)
        {
            feature_dir.append(FeatureNameCore.data(), FeatureNameCore.size());
        }
        else if (spec.features.size() == 2)
        {
            feature_dir.append(spec.features.back());
        }
        else
        {
            fmt::format_to(std::back_inserter(feature_dir), "all_{}", counter);
        }

        return base_path / feature_dir;
    }
}

namespace vcpkg
{
    static constexpr CommandSwitch TEST_FEATURES_SWITCHES[] = {

        {SwitchAll, msgCmdTestFeaturesAll},
        {SwitchNoCore, msgCmdTestFeaturesNoCore},
        {SwitchNoSeparated, msgCmdTestFeaturesNoSeparated},
        {SwitchNoCombined, msgCmdTestFeaturesNoCombined},
    };

    static constexpr CommandSetting TEST_FEATURES_SETTINGS[] = {
        {SwitchCIFeatureBaseline, msgCmdTestCIFeatureBaseline},
        {SwitchFailingAbiLog, msgCmdTestFeaturesFailingAbis},
        {SwitchFailureLogs, msgCISettingsOptFailureLogs},
    };

    constexpr CommandMetadata CommandTestFeaturesMetadata = {
        "x-test-features",
        msgCmdTestFeaturesSynopsis,
        {"vcpkg x-test-features gdal"},
        "https://learn.microsoft.com/vcpkg/commands/test-features",
        AutocompletePriority::Public,
        0,
        SIZE_MAX,
        {TEST_FEATURES_SWITCHES, TEST_FEATURES_SETTINGS, {}},
        nullptr,
    };

    void command_test_features_and_exit(const VcpkgCmdArguments& args,
                                        const VcpkgPaths& paths,
                                        Triplet target_triplet,
                                        Triplet host_triplet)
    {
        auto& fs = paths.get_filesystem();
        const ParsedArguments options = args.parse_arguments(CommandTestFeaturesMetadata);
        const auto& settings = options.settings;

        const auto all_ports = Util::Sets::contains(options.switches, SwitchAll);

        const auto test_feature_core = !Util::Sets::contains(options.switches, SwitchNoCore);
        const auto test_features_combined = !Util::Sets::contains(options.switches, SwitchNoCombined);
        const auto test_features_separately = !Util::Sets::contains(options.switches, SwitchNoSeparated);

        BinaryCache binary_cache(fs);
        if (!binary_cache.install_providers(args, paths, out_sink))
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        Optional<Path> maybe_build_logs_base_path;
        {
            auto it_failure_logs = settings.find(SwitchFailureLogs);
            if (it_failure_logs != settings.end())
            {
                msg::println(msgCreateFailureLogsDir, msg::path = it_failure_logs->second);
                Path raw_path = it_failure_logs->second;
                fs.create_directories(raw_path, VCPKG_LINE_INFO);
                maybe_build_logs_base_path.emplace(fs.almost_canonical(raw_path, VCPKG_LINE_INFO));
            }
        }

        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set, make_overlay_provider(fs, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        std::vector<SourceControlFile*> feature_test_ports;
        if (all_ports)
        {
            const auto files = provider.load_all_control_files();
            feature_test_ports = Util::fmap(files, [](auto& scfl) { return scfl->source_control_file.get(); });
        }
        else
        {
            feature_test_ports = Util::fmap(options.command_arguments, [&](auto&& arg) {
                return provider.get_control_file(arg).value_or_exit(VCPKG_LINE_INFO).source_control_file.get();
            });
        }

        auto feature_baseline_iter = settings.find(SwitchCIFeatureBaseline);
        CiFeatureBaseline feature_baseline;
        if (feature_baseline_iter != settings.end())
        {
            const auto ci_feature_baseline_file_name = feature_baseline_iter->second;
            const auto ci_feature_baseline_file_contents =
                fs.read_contents(ci_feature_baseline_file_name, VCPKG_LINE_INFO);
            ParseMessages ci_parse_messages;
            feature_baseline = parse_ci_feature_baseline(ci_feature_baseline_file_contents,
                                                         ci_feature_baseline_file_name,
                                                         ci_parse_messages,
                                                         target_triplet,
                                                         host_triplet,
                                                         var_provider);
            ci_parse_messages.exit_if_errors_or_warnings(ci_feature_baseline_file_name);
        }

        // to reduce number of cmake invocations
        auto all_specs = Util::fmap(feature_test_ports,
                                    [&](auto scf) { return PackageSpec(scf->core_paragraph->name, target_triplet); });
        var_provider.load_dep_info_vars(all_specs, host_triplet);

        PackagesDirAssigner packages_dir_assigner{paths.packages()};
        CreateInstallPlanOptions install_plan_options{
            nullptr, host_triplet, UnsupportedPortAction::Warn, UseHeadVersion::No, Editable::No};
        static constexpr BuildPackageOptions build_options{
            BuildMissing::Yes,
            AllowDownloads::Yes,
            OnlyDownloads::No,
            CleanBuildtrees::Yes,
            CleanPackages::Yes,
            CleanDownloads::No,
            BackcompatFeatures::Prohibit,
            KeepGoing::Yes,
        };
        StatusParagraphs status_db = database_load_collapse(fs, paths.installed());
        PortDirAbiInfoCache port_dir_abi_info_cache;

        // check what should be tested
        std::vector<FullPackageSpec> specs_to_test;
        for (const auto port : feature_test_ports)
        {
            const auto& baseline = feature_baseline.get_port(port->core_paragraph->name);
            if (baseline.state == CiFeatureBaselineState::Skip) continue;
            PackageSpec package_spec(port->core_paragraph->name, target_triplet);
            const auto dep_info_vars = var_provider.get_or_load_dep_info_vars(package_spec, host_triplet);
            if (!port->core_paragraph->supports_expression.evaluate(dep_info_vars))
            {
                msg::println(
                    msgPortNotSupported, msg::package_name = port->core_paragraph->name, msg::triplet = target_triplet);
                continue;
            }
            if (test_feature_core && !Util::Sets::contains(baseline.skip_features, FeatureNameCore))
            {
                specs_to_test.emplace_back(package_spec, InternalFeatureSet{{FeatureNameCore.to_string()}});
                for (const auto& option_set : baseline.options)
                {
                    if (option_set.front() != FeatureNameCore)
                    {
                        specs_to_test.back().features.push_back(option_set.front());
                    }
                }
            }
            InternalFeatureSet all_features{{FeatureNameCore.to_string()}};
            for (const auto& feature : port->feature_paragraphs)
            {
                if (feature->supports_expression.evaluate(dep_info_vars) &&
                    !Util::Sets::contains(baseline.skip_features, feature->name))
                {
                    // if we expect a feature to cascade or fail don't add it the the all features test because this
                    // test will them simply cascade or fail too
                    if (!Util::Sets::contains(baseline.cascade_features, feature->name) &&
                        !Util::Sets::contains(baseline.failing_features, feature->name))
                    {
                        if (Util::all_of(baseline.options, [&](const auto& options) {
                                return !Util::contains(options, feature->name) || options.front() == feature->name;
                            }))
                        {
                            all_features.push_back(feature->name);
                        }
                    }
                    if (test_features_separately &&
                        !Util::Sets::contains(baseline.no_separate_feature_test, feature->name))
                    {
                        specs_to_test.emplace_back(package_spec,
                                                   InternalFeatureSet{{FeatureNameCore.to_string(), feature->name}});
                        for (const auto& option_set : baseline.options)
                        {
                            if (option_set.front() != FeatureNameCore && !Util::contains(option_set, feature->name))
                            {
                                specs_to_test.back().features.push_back(option_set.front());
                            }
                        }
                    }
                }
            }
            // if test_features_separately == true and there is only one feature test_features_combined is not needed
            if (test_features_combined && all_features.size() > (test_features_separately ? size_t{2} : size_t{1}))
            {
                specs_to_test.emplace_back(package_spec, all_features);
            }
        }

        msg::println(msgComputeInstallPlans, msg::count = specs_to_test.size());

        std::vector<FullPackageSpec> specs;
        std::vector<Path> port_locations;
        std::vector<const InstallPlanAction*> actions_to_check;
        auto install_plans = Util::fmap(specs_to_test, [&](auto& spec) {
            auto install_plan = create_feature_install_plan(provider,
                                                            var_provider,
                                                            Span<FullPackageSpec>(&spec, 1),
                                                            {},
                                                            packages_dir_assigner,
                                                            install_plan_options);
            if (install_plan.unsupported_features.empty())
            {
                for (auto& actions : install_plan.install_actions)
                {
                    specs.emplace_back(actions.spec, actions.feature_list);
                    port_locations.emplace_back(
                        actions.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).port_directory());
                }
                actions_to_check.push_back(&install_plan.install_actions.back());
            }
            return std::make_pair(spec, std::move(install_plan));
        });
        msg::println(msgComputeAllAbis);
        var_provider.load_tag_vars(specs, port_locations, host_triplet);
        for (auto& [spec, install_plan] : install_plans)
        {
            if (install_plan.unsupported_features.empty())
            {
                compute_all_abis(paths, install_plan, var_provider, status_db, port_dir_abi_info_cache);
            }
        }

        msg::println(msgPrecheckBinaryCache);
        binary_cache.precheck(actions_to_check);

        Util::stable_sort(install_plans,
                          [](const std::pair<FullPackageSpec, ActionPlan>& left,
                             const std::pair<FullPackageSpec, ActionPlan>& right) noexcept {
                              return left.second.install_actions.size() < right.second.install_actions.size();
                          });

        // test port features
        std::unordered_set<std::string> known_failures;
        struct UnexpectedResult
        {
            FullPackageSpec spec;
            CiFeatureBaselineState actual_state;
            ElapsedTime build_time;
            std::string cascade_reason;
        };

        std::vector<UnexpectedResult> unexpected_states;

        const auto handle_result = [&](FullPackageSpec&& spec,
                                       CiFeatureBaselineState result,
                                       CiFeatureBaselineEntry baseline,
                                       std::string cascade_reason,
                                       ElapsedTime build_time) {
            bool expected_cascade =
                (baseline.state == CiFeatureBaselineState::Cascade ||
                 (spec.features.size() > 1 && Util::Sets::contains(baseline.cascade_features, spec.features[1])));
            bool actual_cascade = (result == CiFeatureBaselineState::Cascade);
            if (actual_cascade != expected_cascade)
            {
                unexpected_states.push_back(
                    UnexpectedResult{std::move(spec), result, build_time, std::move(cascade_reason)});
                return;
            }
            bool expected_fail = (baseline.state == CiFeatureBaselineState::Fail || baseline.will_fail(spec.features));
            bool actual_fail = (result == CiFeatureBaselineState::Fail);
            if (expected_fail != actual_fail)
            {
                unexpected_states.push_back(
                    UnexpectedResult{std::move(spec), result, build_time, std::move(cascade_reason)});
            }
        };

        for (std::size_t i = 0; i < install_plans.size(); ++i)
        {
            auto& plan_record = install_plans[i];
            auto& spec = plan_record.first;
            auto& install_plan = plan_record.second;
            msg::println(msgStartingFeatureTest,
                         msg::value = fmt::format("{}/{}", i + 1, install_plans.size()),
                         msg::feature_spec = spec);
            const auto& baseline = feature_baseline.get_port(spec.package_spec.name());

            if (!install_plan.unsupported_features.empty())
            {
                std::vector<std::string> out;
                for (const auto& entry : install_plan.unsupported_features)
                {
                    out.push_back(msg::format(msgOnlySupports,
                                              msg::feature_spec = entry.first,
                                              msg::supports_expression = to_string(entry.second))
                                      .extract_data());
                }
                msg::print(msg::format(msgSkipTestingOfPort,
                                       msg::feature_spec = install_plan.install_actions.back().display_name(),
                                       msg::triplet = target_triplet)
                               .append_raw('\n')
                               .append_raw(Strings::join("\n", out))
                               .append_raw('\n'));
                handle_result(std::move(spec),
                              CiFeatureBaselineState::Cascade,
                              baseline,
                              Strings::join(", ", out),
                              ElapsedTime{});
                continue;
            }

            if (auto iter = Util::find_if(install_plan.install_actions,
                                          [&known_failures](const auto& install_action) {
                                              return Util::Sets::contains(
                                                  known_failures,
                                                  install_action.package_abi().value_or_exit(VCPKG_LINE_INFO));
                                          });
                iter != install_plan.install_actions.end())
            {
                msg::println(msgDependencyWillFail, msg::feature_spec = iter->display_name());
                handle_result(
                    std::move(spec), CiFeatureBaselineState::Cascade, baseline, iter->display_name(), ElapsedTime{});
                continue;
            }

            // only install the absolute minimum
            adjust_action_plan_to_status_db(install_plan, status_db);
            if (install_plan.install_actions.empty()) // already installed
            {
                msg::println(msgAlreadyInstalled, msg::spec = spec);
                handle_result(std::move(spec), CiFeatureBaselineState::Pass, baseline, std::string{}, ElapsedTime{});
                continue;
            }

            {
                const InstallPlanAction* action = &install_plan.install_actions.back();
                if (binary_cache.precheck(View<const InstallPlanAction*>(&action, 1)).front() ==
                    CacheAvailability::available)
                {
                    msg::println(msgSkipTestingOfPortAlreadyInBinaryCache,
                                 msg::sha = action->package_abi().value_or_exit(VCPKG_LINE_INFO));
                    handle_result(
                        std::move(spec), CiFeatureBaselineState::Pass, baseline, std::string{}, ElapsedTime{});
                    continue;
                }
            }

            const IBuildLogsRecorder* build_logs_recorder = &null_build_logs_recorder;
            Optional<Path> maybe_logs_dir;
            Optional<CiBuildLogsRecorder> feature_build_logs_recorder_storage;
            if (auto build_logs_base_path = maybe_build_logs_base_path.get())
            {
                auto& logs_dir =
                    maybe_logs_dir.emplace(ci_build_log_feature_test_base_path(*build_logs_base_path, i, spec));
                fs.create_directory(logs_dir, VCPKG_LINE_INFO);
                fs.write_contents(logs_dir / FileTestedSpecDotTxt, spec.to_string(), VCPKG_LINE_INFO);
                build_logs_recorder = &(feature_build_logs_recorder_storage.emplace(logs_dir));
            }

            ElapsedTimer install_timer;
            install_clear_installed_packages(paths, install_plan.install_actions);
            binary_cache.fetch(install_plan.install_actions);
            const auto summary = install_execute_plan(args,
                                                      paths,
                                                      host_triplet,
                                                      build_options,
                                                      install_plan,
                                                      status_db,
                                                      binary_cache,
                                                      *build_logs_recorder,
                                                      false);
            binary_cache.mark_all_unrestored();
            std::string failed_dependencies;
            for (const auto& result : summary.results)
            {
                switch (result.build_result.value_or_exit(VCPKG_LINE_INFO).code)
                {
                    case BuildResult::BuildFailed:
                        if (Path* logs_dir = maybe_logs_dir.get())
                        {
                            auto issue_body_path = *logs_dir / FileIssueBodyMD;
                            fs.write_contents(
                                issue_body_path,
                                create_github_issue(args,
                                                    result.build_result.value_or_exit(VCPKG_LINE_INFO),
                                                    paths,
                                                    result.get_install_plan_action().value_or_exit(VCPKG_LINE_INFO),
                                                    false),
                                VCPKG_LINE_INFO);
                        }
                        if (result.get_spec() != spec.package_spec)
                        {
                            if (!failed_dependencies.empty())
                            {
                                Strings::append(failed_dependencies, ", ");
                            }
                            Strings::append(failed_dependencies, result.get_spec());
                        }
                        [[fallthrough]];
                    case BuildResult::PostBuildChecksFailed:
                        known_failures.insert(result.get_abi().value_or_exit(VCPKG_LINE_INFO));
                        break;
                    default: break;
                }
            }
            const auto time_to_install = install_timer.elapsed();
            switch (summary.results.back().build_result.value_or_exit(VCPKG_LINE_INFO).code)
            {
                case BuildResult::Downloaded:
                case vcpkg::BuildResult::Succeeded:
                    handle_result(std::move(spec), CiFeatureBaselineState::Pass, baseline, {}, time_to_install);
                    break;
                case vcpkg::BuildResult::CascadedDueToMissingDependencies:
                    handle_result(std::move(spec),
                                  CiFeatureBaselineState::Cascade,
                                  baseline,
                                  std::move(failed_dependencies),
                                  time_to_install);
                    break;
                case BuildResult::BuildFailed:
                case BuildResult::PostBuildChecksFailed:
                case BuildResult::FileConflicts:
                case BuildResult::CacheMissing:
                case BuildResult::Removed:
                case BuildResult::Excluded:
                    if (auto abi = summary.results.back().get_abi().get())
                    {
                        known_failures.insert(*abi);
                    }
                    handle_result(std::move(spec), CiFeatureBaselineState::Fail, baseline, {}, time_to_install);
                    break;
            }

            msg::println();
        }

        for (const auto& result : unexpected_states)
        {
            if (result.actual_state == CiFeatureBaselineState::Cascade && !result.cascade_reason.empty())
            {
                msg::println(Color::error,
                             msg::format(msgUnexpectedStateCascade,
                                         msg::feature_spec = result.spec,
                                         msg::actual = result.actual_state)
                                 .append_raw(" ")
                                 .append_raw(result.cascade_reason));
            }
            else
            {
                msg::println(Color::error,
                             msg::format(msgUnexpectedState,
                                         msg::feature_spec = result.spec,
                                         msg::actual = result.actual_state,
                                         msg::elapsed = result.build_time));
            }
        }
        if (unexpected_states.empty())
        {
            msg::println(msgAllFeatureTestsPassed);
        }

        auto it_output_file = settings.find(SwitchFailingAbiLog);
        if (it_output_file != settings.end())
        {
            Path raw_path = it_output_file->second;
            auto content = Strings::join("\n", known_failures);
            content += '\n';
            fs.write_contents_and_dirs(raw_path, content, VCPKG_LINE_INFO);
        }

        binary_cache.wait_for_async_complete_and_join();

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
