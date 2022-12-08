#include <vcpkg/base/cache.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/graphs.h>
#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/view.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/build.h>
#include <vcpkg/ci-baseline.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.ci.h>
#include <vcpkg/commands.setinstalled.h>
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
            auto new_base_path = base_path / Strings::concat("feature_test_", ++counter);
            filesystem.create_directory(new_base_path, VCPKG_LINE_INFO);
            filesystem.write_contents(new_base_path / "tested_spec.txt", spec.to_string(), VCPKG_LINE_INFO);
            return {new_base_path};
        }

        Path base_path;
    };
}

namespace vcpkg::Commands::CI
{
    static constexpr StringLiteral OPTION_DRY_RUN = "dry-run";
    static constexpr StringLiteral OPTION_EXCLUDE = "exclude";
    static constexpr StringLiteral OPTION_HOST_EXCLUDE = "host-exclude";
    static constexpr StringLiteral OPTION_FAILURE_LOGS = "failure-logs";
    static constexpr StringLiteral OPTION_XUNIT = "x-xunit";
    static constexpr StringLiteral OPTION_XUNIT_ALL = "x-xunit-all";
    static constexpr StringLiteral OPTION_CI_BASELINE = "ci-baseline";
    static constexpr StringLiteral OPTION_CI_FEATURE_BASELINE = "ci-feature-baseline";
    static constexpr StringLiteral OPTION_ALLOW_UNEXPECTED_PASSING = "allow-unexpected-passing";
    static constexpr StringLiteral OPTION_SKIP_FAILURES = "skip-failures";
    static constexpr StringLiteral OPTION_RANDOMIZE = "x-randomize";
    static constexpr StringLiteral OPTION_OUTPUT_HASHES = "output-hashes";
    static constexpr StringLiteral OPTION_PARENT_HASHES = "parent-hashes";
    static constexpr StringLiteral OPTION_SKIPPED_CASCADE_COUNT = "x-skipped-cascade-count";
    static constexpr StringLiteral OPTION_TEST_FEATURE_CORE = "test-feature-core";
    static constexpr StringLiteral OPTION_TEST_FEATURES_COMBINED = "test-features-combined";
    static constexpr StringLiteral OPTION_TEST_FEATURES_SEPARATELY = "test-features-separately";
    static constexpr StringLiteral OPTION_RUN_FEATURE_TESTS_PORTS = "run-feature-tests-for-ports";
    static constexpr StringLiteral OPTION_RUN_FEATURE_TESTS_ALL_PORTS = "run-feature-tests-for-all-ports";

    static constexpr std::array<CommandSetting, 10> CI_SETTINGS = {{
        {OPTION_EXCLUDE, "Comma separated list of ports to skip"},
        {OPTION_HOST_EXCLUDE, "Comma separated list of ports to skip for the host triplet"},
        {OPTION_XUNIT, "File to output results in XUnit format (internal)"},
        {OPTION_CI_BASELINE, "Path to the ci.baseline.txt file. Used to skip ports and detect regressions."},
        {OPTION_CI_FEATURE_BASELINE,
         "Path to the ci.feature.baseline.txt file. Used to skip ports and detect regressions."},
        {OPTION_FAILURE_LOGS, "Directory to which failure logs will be copied"},
        {OPTION_OUTPUT_HASHES, "File to output all determined package hashes"},
        {OPTION_PARENT_HASHES,
         "File to read package hashes for a parent CI state, to reduce the set of changed packages"},
        {OPTION_SKIPPED_CASCADE_COUNT,
         "Asserts that the number of --exclude and supports skips exactly equal this number"},
        {OPTION_RUN_FEATURE_TESTS_PORTS,
         "A comma seperated list of ports for which the specified feature tests should be run"},
    }};

    static constexpr std::array<CommandSwitch, 9> CI_SWITCHES = {{
        {OPTION_DRY_RUN, "Print out plan without execution"},
        {OPTION_RANDOMIZE, "Randomize the install order"},
        {OPTION_ALLOW_UNEXPECTED_PASSING,
         "Indicates that 'Passing, remove from fail list' results should not be emitted."},
        {OPTION_SKIP_FAILURES, "Indicates that ports marked `=fail` in ci.baseline.txt should be skipped."},
        {OPTION_XUNIT_ALL, "Report also unchanged ports to the XUnit output (internal)"},
        {OPTION_RUN_FEATURE_TESTS_ALL_PORTS, "Runs the specified tests for all ports"},
        {OPTION_TEST_FEATURE_CORE, "Tests the 'core' feature for every specified port"},
        {OPTION_TEST_FEATURES_SEPARATELY, "Tests every feature of a port seperatly for every specified port"},
        {OPTION_TEST_FEATURES_COMBINED, "Tests the combination of every feature of a port for every specified port"},
    }};

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("ci --triplet=x64-windows"),
        0,
        0,
        {CI_SWITCHES, CI_SETTINGS},
        nullptr,
    };

    struct UnknownCIPortsResults
    {
        std::map<PackageSpec, BuildResult> known;
        std::map<PackageSpec, std::vector<std::string>> features;
        std::map<PackageSpec, std::string> abi_map;
        // action_state_string.size() will equal install_actions.size()
        std::vector<StringLiteral> action_state_string;
        int cascade_count = 0;
    };

    static bool supported_for_triplet(const CMakeVars::CMakeVarProvider& var_provider,
                                      const InstallPlanAction* install_plan)
    {
        auto&& scfl = install_plan->source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
        const auto& supports_expression = scfl.source_control_file->core_paragraph->supports_expression;
        PlatformExpression::Context context =
            var_provider.get_tag_vars(install_plan->spec).value_or_exit(VCPKG_LINE_INFO);

        return supports_expression.evaluate(context);
    }

    static ActionPlan compute_full_plan(const VcpkgPaths& paths,
                                        const PortFileProvider& provider,
                                        const CMakeVars::CMakeVarProvider& var_provider,
                                        const std::vector<FullPackageSpec>& specs,
                                        const CreateInstallPlanOptions& serialize_options)
    {
        std::vector<PackageSpec> packages_with_qualified_deps;
        for (auto&& spec : specs)
        {
            auto&& scfl = provider.get_control_file(spec.package_spec.name()).value_or_exit(VCPKG_LINE_INFO);
            if (scfl.source_control_file->has_qualified_dependencies())
            {
                packages_with_qualified_deps.push_back(spec.package_spec);
            }
        }

        var_provider.load_dep_info_vars(packages_with_qualified_deps, serialize_options.host_triplet);
        auto action_plan = create_feature_install_plan(provider, var_provider, specs, {}, serialize_options);

        var_provider.load_tag_vars(action_plan, provider, serialize_options.host_triplet);

        Checks::check_exit(VCPKG_LINE_INFO, action_plan.already_installed.empty());
        Checks::check_exit(VCPKG_LINE_INFO, action_plan.remove_actions.empty());

        compute_all_abis(paths, action_plan, var_provider, {});
        return action_plan;
    }

    static std::unique_ptr<UnknownCIPortsResults> compute_action_statuses(
        ExclusionPredicate is_excluded,
        const CMakeVars::CMakeVarProvider& var_provider,
        const std::vector<CacheAvailability>& precheck_results,
        const ActionPlan& action_plan)
    {
        auto ret = std::make_unique<UnknownCIPortsResults>();

        std::set<PackageSpec> will_fail;

        ret->action_state_string.reserve(action_plan.install_actions.size());
        for (size_t action_idx = 0; action_idx < action_plan.install_actions.size(); ++action_idx)
        {
            auto&& action = action_plan.install_actions[action_idx];

            auto p = &action;
            ret->abi_map.emplace(action.spec, action.abi_info.value_or_exit(VCPKG_LINE_INFO).package_abi);
            ret->features.emplace(action.spec, action.feature_list);

            if (is_excluded(p->spec))
            {
                ret->action_state_string.emplace_back("skip");
                ret->known.emplace(p->spec, BuildResult::EXCLUDED);
                will_fail.emplace(p->spec);
            }
            else if (!supported_for_triplet(var_provider, p))
            {
                // This treats unsupported ports as if they are excluded
                // which means the ports dependent on it will be cascaded due to missing dependencies
                // Should this be changed so instead it is a failure to depend on a unsupported port?
                ret->action_state_string.emplace_back("n/a");
                ret->known.emplace(p->spec, BuildResult::EXCLUDED);
                will_fail.emplace(p->spec);
            }
            else if (Util::any_of(p->package_dependencies,
                                  [&](const PackageSpec& spec) { return Util::Sets::contains(will_fail, spec); }))
            {
                ret->action_state_string.emplace_back("cascade");
                ret->cascade_count++;
                ret->known.emplace(p->spec, BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES);
                will_fail.emplace(p->spec);
            }
            else if (precheck_results[action_idx] == CacheAvailability::available)
            {
                ret->action_state_string.emplace_back("pass");
                ret->known.emplace(p->spec, BuildResult::SUCCEEDED);
            }
            else
            {
                ret->action_state_string.emplace_back("*");
            }
        }
        return ret;
    }

    // This algorithm reduces an action plan to only unknown actions and their dependencies
    static void reduce_action_plan(ActionPlan& action_plan,
                                   const std::map<PackageSpec, BuildResult>& known,
                                   View<std::string> parent_hashes)
    {
        std::set<PackageSpec> to_keep;
        for (auto it = action_plan.install_actions.rbegin(); it != action_plan.install_actions.rend(); ++it)
        {
            auto it_known = known.find(it->spec);
            const auto& abi = it->abi_info.value_or_exit(VCPKG_LINE_INFO).package_abi;
            auto it_parent = std::find(parent_hashes.begin(), parent_hashes.end(), abi);
            if (it_known == known.end() && it_parent == parent_hashes.end())
            {
                to_keep.insert(it->spec);
            }

            if (Util::Sets::contains(to_keep, it->spec))
            {
                if (it_known != known.end() && it_known->second == BuildResult::EXCLUDED)
                {
                    it->plan_type = InstallPlanType::EXCLUDED;
                }
                else
                {
                    it->build_options = backcompat_prohibiting_package_options;
                    to_keep.insert(it->package_dependencies.begin(), it->package_dependencies.end());
                }
            }
        }

        Util::erase_remove_if(action_plan.install_actions, [&to_keep](const InstallPlanAction& action) {
            return !Util::Sets::contains(to_keep, action.spec);
        });
    }

    static auto get_ports_to_test_with_features(const ParsedArguments& args,
                                                const std::map<PackageSpec, BuildResult>& known_states,
                                                ActionPlan& action_plan)
    {
        const auto all_ports = Util::Sets::contains(args.switches, OPTION_RUN_FEATURE_TESTS_ALL_PORTS);
        std::vector<std::string> ports;
        if (auto iter = args.settings.find(OPTION_RUN_FEATURE_TESTS_PORTS); iter != args.settings.end())
        {
            ports = Strings::split(iter->second, ',');
        }
        std::vector<SourceControlFile*> ports_to_test;
        if (all_ports || !ports.empty())
        {
            for (const auto& action : action_plan.install_actions)
            {
                auto iter = known_states.find(action.spec);
                if (iter != known_states.end() && iter->second == BuildResult::EXCLUDED) continue;
                const auto& source_control_file =
                    action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).source_control_file;

                if (all_ports || Util::Vectors::contains(ports, source_control_file->core_paragraph->name))
                {
                    ports_to_test.push_back(action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO)
                                                .source_control_file.get());
                }
            }
        }
        return ports_to_test;
    }

    static void parse_exclusions(const std::map<std::string, std::string, std::less<>>& settings,
                                 StringLiteral opt,
                                 Triplet triplet,
                                 ExclusionsMap& exclusions_map)
    {
        auto it_exclusions = settings.find(opt);
        exclusions_map.insert(triplet,
                              it_exclusions == settings.end()
                                  ? SortedVector<std::string>{}
                                  : SortedVector<std::string>(Strings::split(it_exclusions->second, ',')));
    }

    static Optional<int> parse_skipped_cascade_count(const std::map<std::string, std::string, std::less<>>& settings)
    {
        auto opt = settings.find(OPTION_SKIPPED_CASCADE_COUNT);
        if (opt == settings.end())
        {
            return nullopt;
        }

        auto result = Strings::strto<int>(opt->second);
        Checks::msg_check_exit(
            VCPKG_LINE_INFO, result.has_value(), msgInvalidArgMustBeAnInt, msg::option = OPTION_SKIPPED_CASCADE_COUNT);
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               result.value_or_exit(VCPKG_LINE_INFO) >= 0,
                               msgInvalidArgMustBePositive,
                               msg::option = OPTION_SKIPPED_CASCADE_COUNT);
        return result;
    }

    static void print_baseline_regressions(const std::vector<SpecSummary>& results,
                                           const std::map<PackageSpec, BuildResult>& known,
                                           const CiBaselineData& cidata,
                                           const std::string& ci_baseline_file_name,
                                           bool allow_unexpected_passing)
    {
        bool has_error = false;
        LocalizedString output = msg::format(msgCiBaselineRegressionHeader);
        output.append_raw('\n');
        for (auto&& r : results)
        {
            auto result = r.build_result.value_or_exit(VCPKG_LINE_INFO).code;
            auto msg = format_ci_result(r.get_spec(), result, cidata, ci_baseline_file_name, allow_unexpected_passing);
            if (!msg.empty())
            {
                has_error = true;
                output.append(msg).append_raw('\n');
            }
        }
        for (auto&& r : known)
        {
            auto msg = format_ci_result(r.first, r.second, cidata, ci_baseline_file_name, allow_unexpected_passing);
            if (!msg.empty())
            {
                has_error = true;
                output.append(msg).append_raw('\n');
            }
        }

        if (!has_error)
        {
            return;
        }
        auto output_data = output.extract_data();
        fwrite(output_data.data(), 1, output_data.size(), stderr);
    }

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet target_triplet,
                          Triplet host_triplet)
    {
        msg::println_warning(msgInternalCICommand);

        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);
        const auto& settings = options.settings;

        const auto test_feature_core = Util::Sets::contains(options.switches, OPTION_TEST_FEATURE_CORE);
        const auto test_features_combined = Util::Sets::contains(options.switches, OPTION_TEST_FEATURES_COMBINED);
        const auto test_features_seperatly = Util::Sets::contains(options.switches, OPTION_TEST_FEATURES_SEPARATELY);

        const auto run_tests_all_ports = Util::Sets::contains(options.switches, OPTION_RUN_FEATURE_TESTS_ALL_PORTS);
        const auto run_tests_ports_list = Util::Sets::contains(options.settings, OPTION_RUN_FEATURE_TESTS_PORTS);
        {
            const auto tests_selected = test_feature_core || test_features_combined || test_features_seperatly;
            const auto ports_selected = run_tests_all_ports || run_tests_ports_list;
            Checks::check_exit(VCPKG_LINE_INFO,
                               (tests_selected && ports_selected) || (!tests_selected && !ports_selected),
                               "You specify a flag to test features, but not which port should be checked");
        }

        BinaryCache binary_cache{args, paths};

        ExclusionsMap exclusions_map;
        parse_exclusions(settings, OPTION_EXCLUDE, target_triplet, exclusions_map);
        parse_exclusions(settings, OPTION_HOST_EXCLUDE, host_triplet, exclusions_map);
        auto baseline_iter = settings.find(OPTION_CI_BASELINE);
        const bool allow_unexpected_passing = Util::Sets::contains(options.switches, OPTION_ALLOW_UNEXPECTED_PASSING);
        CiBaselineData cidata;
        if (baseline_iter == settings.end())
        {
            if (allow_unexpected_passing)
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgCiBaselineAllowUnexpectedPassingRequiresBaseline);
            }
        }
        else
        {
            auto skip_failures =
                Util::Sets::contains(options.switches, OPTION_SKIP_FAILURES) ? SkipFailures::Yes : SkipFailures::No;
            const auto& ci_baseline_file_name = baseline_iter->second;
            const auto ci_baseline_file_contents =
                paths.get_filesystem().read_contents(ci_baseline_file_name, VCPKG_LINE_INFO);
            ParseMessages ci_parse_messages;
            const auto lines = parse_ci_baseline(ci_baseline_file_contents, ci_baseline_file_name, ci_parse_messages);
            ci_parse_messages.exit_if_errors_or_warnings(ci_baseline_file_name);
            cidata = parse_and_apply_ci_baseline(lines, exclusions_map, skip_failures);
        }

        auto skipped_cascade_count = parse_skipped_cascade_count(settings);

        const auto is_dry_run = Util::Sets::contains(options.switches, OPTION_DRY_RUN);

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

        PathsPortFileProvider provider(paths, make_overlay_provider(paths, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        const ElapsedTimer timer;
        std::vector<std::string> all_port_names =
            Util::fmap(provider.load_all_control_files(), Paragraphs::get_name_of_control_file);
        // Install the default features for every package
        std::vector<FullPackageSpec> all_default_full_specs;
        all_default_full_specs.reserve(all_port_names.size());
        for (auto&& port_name : all_port_names)
        {
            all_default_full_specs.emplace_back(PackageSpec{std::move(port_name), target_triplet},
                                                InternalFeatureSet{"core", "default"});
        }

        CreateInstallPlanOptions serialize_options(host_triplet, UnsupportedPortAction::Warn);

        struct RandomizerInstance : GraphRandomizer
        {
            virtual int random(int i) override
            {
                if (i <= 1) return 0;
                std::uniform_int_distribution<int> d(0, i - 1);
                return d(e);
            }

            std::random_device e;
        } randomizer_instance;

        if (Util::Sets::contains(options.switches, OPTION_RANDOMIZE))
        {
            serialize_options.randomizer = &randomizer_instance;
        }

        auto action_plan = compute_full_plan(paths, provider, var_provider, all_default_full_specs, serialize_options);
        const auto precheck_results = binary_cache.precheck(action_plan.install_actions);
        auto split_specs =
            compute_action_statuses(ExclusionPredicate{&exclusions_map}, var_provider, precheck_results, action_plan);
        const auto feature_test_ports = get_ports_to_test_with_features(options, split_specs->known, action_plan);
        {
            std::string msg;
            for (size_t i = 0; i < action_plan.install_actions.size(); ++i)
            {
                auto&& action = action_plan.install_actions[i];
                msg += Strings::format("%40s: %8s: %s\n",
                                       action.spec,
                                       split_specs->action_state_string[i],
                                       action.abi_info.value_or_exit(VCPKG_LINE_INFO).package_abi);
            }
            vcpkg::print2(msg);

            auto it_output_hashes = settings.find(OPTION_OUTPUT_HASHES);
            if (it_output_hashes != settings.end())
            {
                const Path output_hash_json = paths.original_cwd / it_output_hashes->second;
                Json::Array arr;
                for (size_t i = 0; i < action_plan.install_actions.size(); ++i)
                {
                    auto&& action = action_plan.install_actions[i];
                    Json::Object obj;
                    obj.insert("name", Json::Value::string(action.spec.name()));
                    obj.insert("triplet", Json::Value::string(action.spec.triplet().canonical_name()));
                    obj.insert("state", Json::Value::string(split_specs->action_state_string[i]));
                    obj.insert("abi", Json::Value::string(action.abi_info.value_or_exit(VCPKG_LINE_INFO).package_abi));
                    arr.push_back(std::move(obj));
                }
                filesystem.write_contents(output_hash_json, Json::stringify(arr), VCPKG_LINE_INFO);
            }
        }

        std::vector<std::string> parent_hashes;

        auto it_parent_hashes = settings.find(OPTION_PARENT_HASHES);
        if (it_parent_hashes != settings.end())
        {
            const Path parent_hashes_path = paths.original_cwd / it_parent_hashes->second;
            auto parsed_json = Json::parse_file(VCPKG_LINE_INFO, filesystem, parent_hashes_path);
            parent_hashes = Util::fmap(parsed_json.first.array(VCPKG_LINE_INFO), [](const auto& json_object) {
                auto abi = json_object.object(VCPKG_LINE_INFO).get("abi");
                Checks::check_exit(VCPKG_LINE_INFO, abi);
#ifdef _MSC_VER
                _Analysis_assume_(abi);
#endif
                return abi->string(VCPKG_LINE_INFO).to_string();
            });
        }
        reduce_action_plan(action_plan, split_specs->known, parent_hashes);

        msg::println(msgElapsedTimeForChecks, msg::elapsed = timer.elapsed());

        if (auto skipped_cascade_count_ptr = skipped_cascade_count.get())
        {
            Checks::msg_check_exit(VCPKG_LINE_INFO,
                                   *skipped_cascade_count_ptr == split_specs->cascade_count,
                                   msgExpectedCascadeFailure,
                                   msg::expected = *skipped_cascade_count_ptr,
                                   msg::actual = split_specs->cascade_count);
        }

        if (is_dry_run)
        {
            print_plan(action_plan, true, paths.builtin_ports_directory());
        }
        else
        {
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
            };

            std::vector<UnexpectedResult> unexpected_states;

            const auto handle_result = [&](FullPackageSpec&& spec,
                                           CiFeatureBaselineState result,
                                           CiFeatureBaselineEntry baseline,
                                           Optional<Path> logs_dir = nullopt) {
                bool expected_cascade =
                    (baseline.state == CiFeatureBaselineState::Cascade ||
                     (spec.features.size() > 1 && Util::all_of(spec.features, [&](const auto& feature) {
                          return feature == "core" || Util::Sets::contains(baseline.cascade_features, feature);
                      })));
                bool actual_cascade = (result == CiFeatureBaselineState::Cascade);
                if (actual_cascade != expected_cascade)
                {
                    unexpected_states.push_back(UnexpectedResult{std::move(spec), result, logs_dir});
                }
                bool expected_fail =
                    (baseline.state == CiFeatureBaselineState::Fail || baseline.will_fail(spec.features));
                bool actual_fail = (result == CiFeatureBaselineState::Fail);
                if (expected_fail != actual_fail)
                {
                    unexpected_states.push_back(UnexpectedResult{std::move(spec), result, logs_dir});
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
                InternalFeatureSet all_features{{"core"}};
                for (const auto& feature : port->feature_paragraphs)
                {
                    if (feature->supports_expression.evaluate(dep_info_vars) &&
                        !Util::Sets::contains(baseline.skip_features, feature->name))
                    {
                        all_features.push_back(feature->name);
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
                            case BuildResult::POST_BUILD_CHECKS_FAILED:
                            case BuildResult::FILE_CONFLICTS:
                                known_failures.insert(result.get_abi().value_or_exit(VCPKG_LINE_INFO));
                                break;
                            default: break;
                        }
                    }
                    switch (summary.results.back().build_result.value_or_exit(VCPKG_LINE_INFO).code)
                    {
                        case BuildResult::DOWNLOADED:
                        case vcpkg::BuildResult::SUCCEEDED:
                            handle_result(std::move(spec), CiFeatureBaselineState::Pass, baseline, logs_dir);
                            break;
                        case vcpkg::BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES:
                            handle_result(std::move(spec), CiFeatureBaselineState::Cascade, baseline, logs_dir);
                            break;
                        case BuildResult::BUILD_FAILED:
                        case BuildResult::POST_BUILD_CHECKS_FAILED:
                        case BuildResult::FILE_CONFLICTS:
                        case BuildResult::CACHE_MISSING:
                        case BuildResult::REMOVED:
                        case BuildResult::EXCLUDED:
                            handle_result(std::move(spec), CiFeatureBaselineState::Fail, baseline, logs_dir);
                            break;
                    }
                }
            }

            for (const auto& result : unexpected_states)
            {
                print2(result.spec,
                       " resulted in the unexpected state ",
                       result.actual_state,
                       " ",
                       result.logs_dir.value_or(""),
                       '\n');
            }

            if (!known_failures.empty())
            {
                // remove known failures from the action_plan.install_actions
                std::set<PackageSpec> known_failure_specs;
                for (const auto& install_action : action_plan.install_actions)
                {
                    if (Util::Sets::contains(known_failures,
                                             install_action.package_abi().value_or_exit(VCPKG_LINE_INFO)))
                    {
                        split_specs->known.emplace(install_action.spec, BuildResult::BUILD_FAILED);
                        known_failure_specs.emplace(install_action.spec);
                    }
                    else if (Util::any_of(install_action.package_dependencies, [&](const PackageSpec& spec) {
                                 return Util::Sets::contains(known_failure_specs, spec);
                             }))
                    {
                        split_specs->known.emplace(install_action.spec,
                                                   BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES);
                        known_failure_specs.emplace(install_action.spec);
                    }
                }
                Util::erase_remove_if(action_plan.install_actions,
                                      [&known_failure_specs](const InstallPlanAction& action) {
                                          return Util::Sets::contains(known_failure_specs, action.spec);
                                      });
            }
            SetInstalled::adjust_action_plan_to_status_db(action_plan, status_db);

            const IBuildLogsRecorder& build_logs_recorder =
                build_logs_recorder_storage ? *(build_logs_recorder_storage.get()) : null_build_logs_recorder();

            auto summary = Install::perform(
                args, action_plan, KeepGoing::YES, paths, status_db, binary_cache, build_logs_recorder, var_provider);

            for (auto&& result : summary.results)
            {
                split_specs->known.erase(result.get_spec());
            }

            msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("\nTriplet: {}\n", target_triplet));
            summary.print();

            if (baseline_iter != settings.end())
            {
                print_baseline_regressions(
                    summary.results, split_specs->known, cidata, baseline_iter->second, allow_unexpected_passing);
            }

            auto it_xunit = settings.find(OPTION_XUNIT);
            if (it_xunit != settings.end())
            {
                XunitWriter xunitTestResults;

                // Adding results for ports that were built or pulled from an archive
                for (auto&& result : summary.results)
                {
                    const auto& spec = result.get_spec();
                    auto& port_features = split_specs->features.at(spec);
                    auto code = result.build_result.value_or_exit(VCPKG_LINE_INFO).code;
                    xunitTestResults.add_test_results(
                        spec, code, result.timing, result.start_time, split_specs->abi_map.at(spec), port_features);
                }

                // Adding results for ports that were not built because they have known states
                if (Util::Sets::contains(options.switches, OPTION_XUNIT_ALL))
                {
                    for (auto&& port : split_specs->known)
                    {
                        const auto& spec = port.first;
                        auto& port_features = split_specs->features.at(spec);
                        xunitTestResults.add_test_results(spec,
                                                          port.second,
                                                          ElapsedTime{},
                                                          std::chrono::system_clock::time_point{},
                                                          split_specs->abi_map.at(spec),
                                                          port_features);
                    }
                }

                filesystem.write_contents(
                    it_xunit->second, xunitTestResults.build_xml(target_triplet), VCPKG_LINE_INFO);
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void CICommand::perform_and_exit(const VcpkgCmdArguments& args,
                                     const VcpkgPaths& paths,
                                     Triplet default_triplet,
                                     Triplet host_triplet) const
    {
        CI::perform_and_exit(args, paths, default_triplet, host_triplet);
    }
}
