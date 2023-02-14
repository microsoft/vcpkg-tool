#include <vcpkg/base/cache.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/graphs.h>
#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/build.h>
#include <vcpkg/ci-baseline.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.setinstalled.h>
#include <vcpkg/commands.test-features.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/install.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/platform-expression.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/xunitwriter.h>

#include <stdio.h>

using namespace vcpkg;

namespace
{
    const Path readme_dot_log = "readme.log";

    struct CiBuildLogsRecorder final : IBuildLogsRecorder
    {
        CiBuildLogsRecorder(const Path& base_path_) : base_path(base_path_) { }

        void record_build_result(const VcpkgPaths& paths, const PackageSpec& spec, BuildResult result) const override
        {
            if (result == BuildResult::SUCCEEDED)
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

        CiBuildLogsRecorder create_for_feature_test(const FullPackageSpec& spec, Filesystem& filesystem) const
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
                feature = Strings::concat("all_", ++counter);
            }
            auto new_base_path = base_path / Strings::concat(spec.package_spec.name(), '_', feature);
            filesystem.create_directory(new_base_path, VCPKG_LINE_INFO);
            filesystem.write_contents(new_base_path / "tested_spec.txt", spec.to_string(), VCPKG_LINE_INFO);
            return {new_base_path};
        }

        Path base_path;
    };
}

namespace vcpkg::Commands::TestFeatures
{
    static constexpr StringLiteral OPTION_FAILURE_LOGS = "failure-logs";
    static constexpr StringLiteral OPTION_CI_FEATURE_BASELINE = "ci-feature-baseline";
    static constexpr StringLiteral OPTION_DONT_TEST_FEATURE_CORE = "dont-test-feature-core";
    static constexpr StringLiteral OPTION_DONT_TEST_FEATURES_COMBINED = "dont-test-features-combined";
    static constexpr StringLiteral OPTION_DONT_TEST_FEATURES_SEPARATELY = "dont-test-features-separately";
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
        {OPTION_DONT_TEST_FEATURE_CORE,
         []() { return LocalizedString::from_raw("Tests the 'core' feature for every specified port"); }},
        {OPTION_DONT_TEST_FEATURES_SEPARATELY,
         []() {
             return LocalizedString::from_raw("Tests every feature of a port seperatly for every specified port");
         }},
        {OPTION_DONT_TEST_FEATURES_COMBINED,
         []() {
             return LocalizedString::from_raw(
                 "Tests the combination of every feature of a port for every specified port");
         }},
    }};

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("test-features llvm"),
        0,
        SIZE_MAX,
        {CI_SWITCHES, CI_SETTINGS},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet target_triplet,
                          Triplet host_triplet)
    {
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);
        const auto& settings = options.settings;

        const auto all_ports = Util::Sets::contains(options.switches, OPTION_ALL_PORTS);

        const auto test_feature_core = !Util::Sets::contains(options.switches, OPTION_DONT_TEST_FEATURE_CORE);
        const auto test_features_combined = !Util::Sets::contains(options.switches, OPTION_DONT_TEST_FEATURES_COMBINED);
        const auto test_features_seperatly =
            !Util::Sets::contains(options.switches, OPTION_DONT_TEST_FEATURES_SEPARATELY);

        BinaryCache binary_cache{args, paths};

        auto& filesystem = paths.get_filesystem();
        Optional<CiBuildLogsRecorder> build_logs_recorder_storage;
        {
            auto it_failure_logs = settings.find(OPTION_FAILURE_LOGS);
            if (it_failure_logs != settings.end())
            {
                msg::println(msgCreateFailureLogsDir, msg::path = it_failure_logs->second);
                Path raw_path = it_failure_logs->second;
                filesystem.create_directories(raw_path, VCPKG_LINE_INFO);
                build_logs_recorder_storage = filesystem.almost_canonical(raw_path, VCPKG_LINE_INFO);
            }
        }

        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(
            filesystem, *registry_set, make_overlay_provider(filesystem, paths.original_cwd, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        // const auto precheck_results = binary_cache.precheck(action_plan.install_actions);

        std::vector<SourceControlFile*> feature_test_ports;
        if (all_ports)
        {
            feature_test_ports = Util::fmap(provider.load_all_control_files(),
                                            [](auto& scfl) { return scfl->source_control_file.get(); });
        }
        else
        {
            feature_test_ports = Util::fmap(args.command_arguments, [&](auto&& arg) {
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

        StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());

        // test port features
        std::unordered_set<std::string> known_failures;
        struct UnexpectedResult
        {
            FullPackageSpec spec;
            CiFeatureBaselineState actual_state;
            Optional<Path> logs_dir;
            ElapsedTime build_time;
        };

        std::vector<UnexpectedResult> unexpected_states;

        const auto handle_result = [&](FullPackageSpec&& spec,
                                       CiFeatureBaselineState result,
                                       CiFeatureBaselineEntry baseline,
                                       Optional<Path> logs_dir = nullopt,
                                       ElapsedTime build_time = {}) {
            bool expected_cascade =
                (baseline.state == CiFeatureBaselineState::Cascade ||
                 (spec.features.size() > 1 && Util::all_of(spec.features, [&](const auto& feature) {
                      return feature == "core" || Util::Sets::contains(baseline.cascade_features, feature);
                  })));
            bool actual_cascade = (result == CiFeatureBaselineState::Cascade);
            if (actual_cascade != expected_cascade)
            {
                unexpected_states.push_back(UnexpectedResult{std::move(spec), result, logs_dir, build_time});
            }
            bool expected_fail = (baseline.state == CiFeatureBaselineState::Fail || baseline.will_fail(spec.features));
            bool actual_fail = (result == CiFeatureBaselineState::Fail);
            if (expected_fail != actual_fail)
            {
                unexpected_states.push_back(UnexpectedResult{std::move(spec), result, logs_dir, build_time});
            }
        };

        int i = 0;
        for (const auto port : feature_test_ports)
        {
            ++i;
            print2("\nTest ", i, "/", feature_test_ports.size(), " ", port->core_paragraph->name, "\n");
            const auto& baseline = feature_baseline.get_port(port->core_paragraph->name);
            if (baseline.state == CiFeatureBaselineState::Skip) continue;
            PackageSpec package_spec(port->core_paragraph->name, target_triplet);
            std::vector<FullPackageSpec> specs_to_test;
            if (test_feature_core && !Util::Sets::contains(baseline.skip_features, "core"))
            {
                specs_to_test.emplace_back(package_spec, InternalFeatureSet{{"core"}});
            }
            const auto dep_info_vars = var_provider.get_or_load_dep_info_vars(package_spec, host_triplet);
            if (!port->core_paragraph->supports_expression.evaluate(dep_info_vars))
            {
                print2("Port is not supported on this triplet\n");
                continue;
            }
            InternalFeatureSet all_features{{"core"}};
            for (const auto& feature : port->feature_paragraphs)
            {
                if (feature->supports_expression.evaluate(dep_info_vars) &&
                    !Util::Sets::contains(baseline.skip_features, feature->name))
                {
                    // if we expect a feature to cascade don't add it the the all features test because this test
                    // will them simply cascade too
                    if (!Util::Sets::contains(baseline.cascade_features, feature->name))
                    {
                        all_features.push_back(feature->name);
                    }
                    if (test_features_seperatly &&
                        !Util::Sets::contains(baseline.no_separate_feature_test, feature->name))
                    {
                        specs_to_test.emplace_back(package_spec, InternalFeatureSet{{"core", feature->name}});
                    }
                }
            }
            // if test_features_seperatly == true and there is only one feature test_features_combined is not needed
            if (test_features_combined && all_features.size() > (test_features_seperatly ? size_t{2} : size_t{1}))
            {
                specs_to_test.emplace_back(package_spec, all_features);
            }

            for (auto&& spec : std::move(specs_to_test))
            {
                auto install_plan = create_feature_install_plan(provider,
                                                                var_provider,
                                                                Span<FullPackageSpec>(&spec, 1),
                                                                {},
                                                                {host_triplet, UnsupportedPortAction::Warn});

                if (!install_plan.warnings.empty())
                {
                    print2("Skipping testing of ",
                           install_plan.install_actions.back().displayname(),
                           " because of the following warnings: \n",
                           Strings::join("\n", install_plan.warnings),
                           '\n');
                    handle_result(std::move(spec), CiFeatureBaselineState::Cascade, baseline);
                    continue;
                }
                var_provider.load_tag_vars(install_plan, provider, host_triplet);
                compute_all_abis(paths, install_plan, var_provider, status_db);

                if (auto iter = Util::find_if(install_plan.install_actions,
                                              [&known_failures](const auto& install_action) {
                                                  return Util::Sets::contains(
                                                      known_failures,
                                                      install_action.package_abi().value_or_exit(VCPKG_LINE_INFO));
                                              });
                    iter != install_plan.install_actions.end())
                {
                    print2(spec, " dependency ", iter->displayname(), " will fail => cascade\n");
                    handle_result(std::move(spec), CiFeatureBaselineState::Cascade, baseline);
                    continue;
                }

                // only install the absolute minimum
                SetInstalled::adjust_action_plan_to_status_db(install_plan, status_db);
                if (install_plan.install_actions.empty()) // already installed
                {
                    handle_result(std::move(spec), CiFeatureBaselineState::Pass, baseline);
                    continue;
                }

                if (binary_cache.precheck({&install_plan.install_actions.back(), 1}).front() ==
                    CacheAvailability::available)
                {
                    handle_result(std::move(spec), CiFeatureBaselineState::Pass, baseline);
                    continue;
                }
                print2("Test feature ", spec, '\n');
                for (auto&& action : install_plan.install_actions)
                {
                    action.build_options = backcompat_prohibiting_package_options;
                }
                Optional<CiBuildLogsRecorder> feature_build_logs_recorder_storage =
                    build_logs_recorder_storage.map([&](const CiBuildLogsRecorder& recorder) {
                        return recorder.create_for_feature_test(spec, filesystem);
                    });
                Optional<Path> logs_dir = feature_build_logs_recorder_storage.map(
                    [](const CiBuildLogsRecorder& recorder) { return recorder.base_path; });
                const IBuildLogsRecorder& build_logs_recorder = feature_build_logs_recorder_storage
                                                                    ? *(feature_build_logs_recorder_storage.get())
                                                                    : null_build_logs_recorder();
                ElapsedTimer install_timer;
                const auto summary = Install::perform(args,
                                                      install_plan,
                                                      KeepGoing::YES,
                                                      paths,
                                                      status_db,
                                                      binary_cache,
                                                      build_logs_recorder,
                                                      var_provider);
                binary_cache.clear_cache();

                for (const auto& result : summary.results)
                {
                    switch (result.build_result.value_or_exit(VCPKG_LINE_INFO).code)
                    {
                        case BuildResult::BUILD_FAILED:
                            if (Path* dir = logs_dir.get())
                            {
                                auto issue_body_path = *dir / "issue_body.md";
                                paths.get_filesystem().write_contents(
                                    issue_body_path,
                                    create_github_issue(
                                        args,
                                        result.build_result.value_or_exit(VCPKG_LINE_INFO),
                                        paths,
                                        result.get_install_plan_action().value_or_exit(VCPKG_LINE_INFO)),
                                    VCPKG_LINE_INFO);
                            }
                            [[fallthrough]];
                        case BuildResult::POST_BUILD_CHECKS_FAILED:
                        case BuildResult::FILE_CONFLICTS:
                            known_failures.insert(result.get_abi().value_or_exit(VCPKG_LINE_INFO));
                            break;
                        default: break;
                    }
                }
                const auto time_to_install = install_timer.elapsed();
                switch (summary.results.back().build_result.value_or_exit(VCPKG_LINE_INFO).code)
                {
                    case BuildResult::DOWNLOADED:
                    case vcpkg::BuildResult::SUCCEEDED:
                        handle_result(
                            std::move(spec), CiFeatureBaselineState::Pass, baseline, logs_dir, time_to_install);
                        break;
                    case vcpkg::BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES:
                        handle_result(
                            std::move(spec), CiFeatureBaselineState::Cascade, baseline, logs_dir, time_to_install);
                        break;
                    case BuildResult::BUILD_FAILED:
                    case BuildResult::POST_BUILD_CHECKS_FAILED:
                    case BuildResult::FILE_CONFLICTS:
                    case BuildResult::CACHE_MISSING:
                    case BuildResult::REMOVED:
                    case BuildResult::EXCLUDED:
                        handle_result(
                            std::move(spec), CiFeatureBaselineState::Fail, baseline, logs_dir, time_to_install);
                        break;
                }
            }
        }

        for (const auto& result : unexpected_states)
        {
            print2(Color::error,
                   result.spec,
                   " resulted in the unexpected state ",
                   result.actual_state,
                   " ",
                   result.logs_dir.value_or(""),
                   " after ",
                   result.build_time.to_string(),
                   '\n');
        }

        auto it_output_file = settings.find(OPTION_OUTPUT_FAILURE_ABIS);
        if (it_output_file != settings.end())
        {
            Path raw_path = it_output_file->second;
            auto content = Strings::join("\n", known_failures);
            content += '\n';
            filesystem.write_contents_and_dirs(raw_path, content, VCPKG_LINE_INFO);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void TestFeaturesCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                               const VcpkgPaths& paths,
                                               Triplet default_triplet,
                                               Triplet host_triplet) const
    {
        TestFeatures::perform_and_exit(args, paths, default_triplet, host_triplet);
    }
}
