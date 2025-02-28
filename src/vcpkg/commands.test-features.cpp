#include <vcpkg/base/cache.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/graphs.h>
#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/ci-feature-baseline.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.help.h>
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
#include <vcpkg/xunitwriter.h>

using namespace vcpkg;

namespace
{
    const Path readme_dot_log = "readme.log";

    struct CiBuildLogsRecorder final : IBuildLogsRecorder
    {
        CiBuildLogsRecorder(const Path& base_path_) : base_path(base_path_) { }

        void record_build_result(const VcpkgPaths& paths, const PackageSpec& spec, BuildResult result) const override
        {
            if (result == BuildResult::Succeeded)
            {
                return;
            }

            auto& filesystem = paths.get_filesystem();
            const auto source_path = paths.build_dir(spec);
            auto children = filesystem.get_regular_files_non_recursive(source_path, IgnoreErrors{});
            Util::erase_remove_if(children, NotExtensionCaseInsensitive{".log"});
            const auto target_path = base_path / spec.name();
            (void)filesystem.create_directories(target_path, VCPKG_LINE_INFO);
            if (children.empty())
            {
                std::string message =
                    "There are no build logs for " + spec.to_string() +
                    " build.\n"
                    "This is usually because the build failed early and outside of a task that is logged.\n"
                    "See the console output logs from vcpkg for more information on the failure.\n";
                filesystem.write_contents(target_path / readme_dot_log, message, VCPKG_LINE_INFO);
            }
            else
            {
                for (const Path& p : children)
                {
                    filesystem.copy_file(
                        p, target_path / p.filename(), CopyOptions::overwrite_existing, VCPKG_LINE_INFO);
                }
            }
        }

        CiBuildLogsRecorder create_for_feature_test(const FullPackageSpec& spec, const Filesystem& filesystem) const
        {
            static int counter = 0;
            std::string feature;
            if (spec.features.size() == 1)
            {
                feature = "core";
            }
            else if (spec.features.size() == 2)
            {
                feature = spec.features.back();
            }
            else
            {
                feature = fmt::format("all_{}", ++counter);
            }
            auto new_base_path = base_path / Strings::concat(spec.package_spec.name(), '_', feature);
            filesystem.create_directory(new_base_path, VCPKG_LINE_INFO);
            filesystem.write_contents(new_base_path / "tested_spec.txt", spec.to_string(), VCPKG_LINE_INFO);
            return {new_base_path};
        }

        Path base_path;
    };
}

namespace vcpkg
{
    static constexpr StringLiteral OPTION_FAILURE_LOGS = "failure-logs";
    static constexpr StringLiteral OPTION_CI_FEATURE_BASELINE = "ci-feature-baseline";
    static constexpr StringLiteral OPTION_NO_FEATURE_CORE_TEST = "no-feature-core-test";
    static constexpr StringLiteral OPTION_NO_FEATURES_COMBINED_TEST = "no-features-combined-test";
    static constexpr StringLiteral OPTION_NO_FEATURES_SEPARATED_TESTS = "no-features-separated-tests";
    static constexpr StringLiteral OPTION_OUTPUT_FAILURE_ABIS = "write-failure-abis-to";
    static constexpr StringLiteral OPTION_ALL_PORTS = "all";

    static constexpr std::array<CommandSetting, 3> CI_SETTINGS = {{
        {OPTION_CI_FEATURE_BASELINE,
         []() {
             return LocalizedString::from_raw(
                 "Path to the ci.feature.baseline.txt file. Used to skip ports and detect regressions.");
         }},
        {OPTION_OUTPUT_FAILURE_ABIS,
         []() {
             return LocalizedString::from_raw(
                 "Path to a file to which abis from ports that failed to build are written.");
         }},
        {OPTION_FAILURE_LOGS, []() { return msg::format(msgCISettingsOptFailureLogs); }},
    }};

    static constexpr std::array<CommandSwitch, 4> CI_SWITCHES = {{

        {OPTION_ALL_PORTS, []() { return LocalizedString::from_raw("Runs the specified tests for all ports"); }},
        {OPTION_NO_FEATURE_CORE_TEST,
         []() { return LocalizedString::from_raw("Tests the 'core' feature for every specified port"); }},
        {OPTION_NO_FEATURES_SEPARATED_TESTS,
         []() {
             return LocalizedString::from_raw("Tests every feature of a port seperatly for every specified port");
         }},
        {OPTION_NO_FEATURES_COMBINED_TEST,
         []() {
             return LocalizedString::from_raw(
                 "Tests the combination of every feature of a port for every specified port");
         }},
    }};

    constexpr CommandMetadata CommandTestFeaturesMetadata = {
        "x-test-features",
        msgCmdTestFeaturesSynopsis,
        {"vcpkg x-test-features gdal"},
        "https://learn.microsoft.com/vcpkg/commands/test-features",
        AutocompletePriority::Public,
        0,
        SIZE_MAX,
        {CI_SWITCHES, CI_SETTINGS, {}},
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

        const auto all_ports = Util::Sets::contains(options.switches, OPTION_ALL_PORTS);

        const auto test_feature_core = !Util::Sets::contains(options.switches, OPTION_NO_FEATURE_CORE_TEST);
        const auto test_features_combined = !Util::Sets::contains(options.switches, OPTION_NO_FEATURES_COMBINED_TEST);
        const auto test_features_seperatly =
            !Util::Sets::contains(options.switches, OPTION_NO_FEATURES_SEPARATED_TESTS);

        BinaryCache binary_cache(fs);
        if (!binary_cache.install_providers(args, paths, out_sink))
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        Optional<CiBuildLogsRecorder> build_logs_recorder_storage;
        {
            auto it_failure_logs = settings.find(OPTION_FAILURE_LOGS);
            if (it_failure_logs != settings.end())
            {
                msg::println(msgCreateFailureLogsDir, msg::path = it_failure_logs->second);
                Path raw_path = it_failure_logs->second;
                fs.create_directories(raw_path, VCPKG_LINE_INFO);
                build_logs_recorder_storage = fs.almost_canonical(raw_path, VCPKG_LINE_INFO);
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

        auto feature_baseline_iter = settings.find(OPTION_CI_FEATURE_BASELINE);
        CiFeatureBaseline feature_baseline;
        if (feature_baseline_iter != settings.end())
        {
            const auto ci_feature_baseline_file_name = feature_baseline_iter->second;
            const auto ci_feature_baseline_file_contents =
                paths.get_filesystem().read_contents(ci_feature_baseline_file_name, VCPKG_LINE_INFO);
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
            if (test_feature_core && !Util::Sets::contains(baseline.skip_features, "core"))
            {
                specs_to_test.emplace_back(package_spec, InternalFeatureSet{{"core"}});
                for (const auto& option_set : baseline.options)
                {
                    if (option_set.front() != "core")
                    {
                        specs_to_test.back().features.push_back(option_set.front());
                    }
                }
            }
            InternalFeatureSet all_features{{"core"}};
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
                    if (test_features_seperatly &&
                        !Util::Sets::contains(baseline.no_separate_feature_test, feature->name))
                    {
                        specs_to_test.emplace_back(package_spec, InternalFeatureSet{{"core", feature->name}});
                        for (const auto& option_set : baseline.options)
                        {
                            if (option_set.front() != "core" && !Util::contains(option_set, feature->name))
                            {
                                specs_to_test.back().features.push_back(option_set.front());
                            }
                        }
                    }
                }
            }
            // if test_features_seperatly == true and there is only one feature test_features_combined is not needed
            if (test_features_combined && all_features.size() > (test_features_seperatly ? size_t{2} : size_t{1}))
            {
                specs_to_test.emplace_back(package_spec, all_features);
            }
        }
        msg::println(msgComputeInstallPlans, msg::count = specs_to_test.size());

        std::vector<FullPackageSpec> specs;
        std::vector<Path> port_locations;
        std::vector<const InstallPlanAction*> actions_to_check;
        CreateInstallPlanOptions install_plan_options{
            nullptr, host_triplet, paths.packages(), UnsupportedPortAction::Warn, UseHeadVersion::No, Editable::No};
        auto install_plans = Util::fmap(specs_to_test, [&](auto& spec) {
            auto install_plan = create_feature_install_plan(
                provider, var_provider, Span<FullPackageSpec>(&spec, 1), {}, install_plan_options);
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
        StatusParagraphs status_db = database_load_collapse(paths.get_filesystem(), paths.installed());
        PortDirAbiInfoCache port_dir_abi_info_cache;
        for (auto& [spec, install_plan] : install_plans)
        {
            if (install_plan.unsupported_features.empty())
            {
                compute_all_abis(paths, install_plan, var_provider, status_db, port_dir_abi_info_cache);
            }
        }

        msg::println(msgPrecheckBinaryCache);
        binary_cache.precheck(actions_to_check);

        Util::stable_sort(install_plans, [](const auto& left, const auto& right) {
            return left.second.install_actions.size() < right.second.install_actions.size();
        });

        // test port features
        std::unordered_set<std::string> known_failures;
        struct UnexpectedResult
        {
            FullPackageSpec spec;
            CiFeatureBaselineState actual_state;
            Optional<Path> logs_dir;
            ElapsedTime build_time;
            std::string cascade_reason;
        };

        std::vector<UnexpectedResult> unexpected_states;

        const auto handle_result = [&](FullPackageSpec&& spec,
                                       CiFeatureBaselineState result,
                                       CiFeatureBaselineEntry baseline,
                                       std::string cascade_reason = {},
                                       Optional<Path> logs_dir = nullopt,
                                       ElapsedTime build_time = {}) {
            bool expected_cascade =
                (baseline.state == CiFeatureBaselineState::Cascade ||
                 (spec.features.size() > 1 && Util::Sets::contains(baseline.cascade_features, spec.features[1])));
            bool actual_cascade = (result == CiFeatureBaselineState::Cascade);
            if (actual_cascade != expected_cascade)
            {
                unexpected_states.push_back(
                    UnexpectedResult{std::move(spec), result, logs_dir, build_time, std::move(cascade_reason)});
                return;
            }
            bool expected_fail = (baseline.state == CiFeatureBaselineState::Fail || baseline.will_fail(spec.features));
            bool actual_fail = (result == CiFeatureBaselineState::Fail);
            if (expected_fail != actual_fail)
            {
                unexpected_states.push_back(
                    UnexpectedResult{std::move(spec), result, logs_dir, build_time, std::move(cascade_reason)});
            }
        };

        int i = 0;
        for (auto&& [spec, install_plan] : std::move(install_plans))
        {
            ++i;
            fmt::print("\n{}/{} {}\n", i, install_plans.size(), spec.to_string());
            const auto& baseline = feature_baseline.get_port(spec.package_spec.name());

            if (!install_plan.unsupported_features.empty())
            {
                std::vector<std::string> out;
                for (const auto& entry : install_plan.unsupported_features)
                {
                    out.push_back(msg::format(msgOnlySupports,
                                              msg::feature_spec = entry.first.to_string(),
                                              msg::supports_expression = to_string(entry.second))
                                      .extract_data());
                }
                msg::print(msg::format(msgSkipTestingOfPort,
                                       msg::feature_spec = install_plan.install_actions.back().display_name(),
                                       msg::triplet = target_triplet)
                               .append_raw('\n')
                               .append_raw(Strings::join("\n", out))
                               .append_raw('\n'));
                handle_result(std::move(spec), CiFeatureBaselineState::Cascade, baseline, Strings::join(", ", out));
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
                handle_result(std::move(spec), CiFeatureBaselineState::Cascade, baseline, iter->display_name());
                continue;
            }

            // only install the absolute minimum
            adjust_action_plan_to_status_db(install_plan, status_db);
            if (install_plan.install_actions.empty()) // already installed
            {
                handle_result(std::move(spec), CiFeatureBaselineState::Pass, baseline);
                continue;
            }

            {
                const InstallPlanAction* action = &install_plan.install_actions.back();
                std::array<const InstallPlanAction*, 1> actions = {action};
                if (binary_cache.precheck(actions).front() == CacheAvailability::available)
                {
                    handle_result(std::move(spec), CiFeatureBaselineState::Pass, baseline);
                    continue;
                }
            }
            Optional<CiBuildLogsRecorder> feature_build_logs_recorder_storage =
                build_logs_recorder_storage.map([&, &spec = spec](const CiBuildLogsRecorder& recorder) {
                    return recorder.create_for_feature_test(spec, fs);
                });
            Optional<Path> logs_dir = feature_build_logs_recorder_storage.map(
                [](const CiBuildLogsRecorder& recorder) { return recorder.base_path; });
            const IBuildLogsRecorder& build_logs_recorder = feature_build_logs_recorder_storage
                                                                ? *(feature_build_logs_recorder_storage.get())
                                                                : null_build_logs_recorder();
            ElapsedTimer install_timer;
            install_preclear_packages(paths, install_plan);
            binary_cache.fetch(install_plan.install_actions);
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
            const auto summary = install_execute_plan(args,
                                                      paths,
                                                      host_triplet,
                                                      build_options,
                                                      install_plan,
                                                      status_db,
                                                      binary_cache,
                                                      build_logs_recorder,
                                                      false);
            binary_cache.mark_all_unrestored();

            std::string failed_dependencies;
            for (const auto& result : summary.results)
            {
                switch (result.build_result.value_or_exit(VCPKG_LINE_INFO).code)
                {
                    case BuildResult::BuildFailed:
                        if (Path* dir = logs_dir.get())
                        {
                            auto issue_body_path = *dir / "issue_body.md";
                            paths.get_filesystem().write_contents(
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
                    handle_result(
                        std::move(spec), CiFeatureBaselineState::Pass, baseline, {}, logs_dir, time_to_install);
                    break;
                case vcpkg::BuildResult::CascadedDueToMissingDependencies:
                    handle_result(std::move(spec),
                                  CiFeatureBaselineState::Cascade,
                                  baseline,
                                  std::move(failed_dependencies),
                                  logs_dir,
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
                    handle_result(
                        std::move(spec), CiFeatureBaselineState::Fail, baseline, {}, logs_dir, time_to_install);
                    break;
            }
        }

        msg::println();
        for (const auto& result : unexpected_states)
        {
            if (result.actual_state == CiFeatureBaselineState::Cascade && !result.cascade_reason.empty())
            {
                msg::println(Color::error,
                             msg::format(msgUnexpectedStateCascase,
                                         msg::feature_spec = to_string(result.spec),
                                         msg::actual = to_string(result.actual_state))
                                 .append_raw(" ")
                                 .append_raw(result.cascade_reason));
            }
            else
            {
                msg::println(Color::error,
                             msg::format(msgUnexpectedState,
                                         msg::feature_spec = to_string(result.spec),
                                         msg::actual = to_string(result.actual_state),
                                         msg::elapsed = result.build_time)
                                 .append_raw(" ")
                                 .append_raw(result.logs_dir.value_or("")));
            }
        }
        if (unexpected_states.empty())
        {
            msg::println(msgAllFeatureTestsPassed);
        }

        auto it_output_file = settings.find(OPTION_OUTPUT_FAILURE_ABIS);
        if (it_output_file != settings.end())
        {
            Path raw_path = it_output_file->second;
            auto content = Strings::join("\n", known_failures);
            content += '\n';
            fs.write_contents_and_dirs(raw_path, content, VCPKG_LINE_INFO);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
