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
#include <vcpkg/ci-baseline.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.ci.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/commands.set-installed.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/globalstate.h>
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

#include <random>

using namespace vcpkg;

namespace
{
    const Path readme_dot_log = "readme.log";

    struct CiBuildLogsRecorder final : IBuildLogsRecorder
    {
        CiBuildLogsRecorder(const Path& base_path_) : base_path(base_path_) { }

        virtual void record_build_result(const VcpkgPaths& paths,
                                         const PackageSpec& spec,
                                         BuildResult result) const override
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
            (void)filesystem.create_directory(target_path, VCPKG_LINE_INFO);
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

    private:
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
    static constexpr StringLiteral OPTION_ALLOW_UNEXPECTED_PASSING = "allow-unexpected-passing";
    static constexpr StringLiteral OPTION_SKIP_FAILURES = "skip-failures";
    static constexpr StringLiteral OPTION_RANDOMIZE = "x-randomize";
    static constexpr StringLiteral OPTION_OUTPUT_HASHES = "output-hashes";
    static constexpr StringLiteral OPTION_PARENT_HASHES = "parent-hashes";

    static constexpr std::array<CommandSetting, 7> CI_SETTINGS = {
        {{OPTION_EXCLUDE, []() { return msg::format(msgCISettingsOptExclude); }},
         {OPTION_HOST_EXCLUDE, []() { return msg::format(msgCISettingsOptHostExclude); }},
         {OPTION_XUNIT, []() { return msg::format(msgCISettingsOptXUnit); }},
         {OPTION_CI_BASELINE, []() { return msg::format(msgCISettingsOptCIBase); }},
         {OPTION_FAILURE_LOGS, []() { return msg::format(msgCISettingsOptFailureLogs); }},
         {OPTION_OUTPUT_HASHES, []() { return msg::format(msgCISettingsOptOutputHashes); }},
         {OPTION_PARENT_HASHES, []() { return msg::format(msgCISettingsOptParentHashes); }}}};

    static constexpr std::array<CommandSwitch, 5> CI_SWITCHES = {{
        {OPTION_DRY_RUN, []() { return msg::format(msgCISwitchOptDryRun); }},
        {OPTION_RANDOMIZE, []() { return msg::format(msgCISwitchOptRandomize); }},
        {OPTION_ALLOW_UNEXPECTED_PASSING, []() { return msg::format(msgCISwitchOptAllowUnexpectedPassing); }},
        {OPTION_SKIP_FAILURES, []() { return msg::format(msgCISwitchOptSkipFailures); }},
        {OPTION_XUNIT_ALL, []() { return msg::format(msgCISwitchOptXUnitAll); }},
    }};

    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("ci --triplet=x64-windows"); },
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
    };

    static bool supported_for_triplet(const CMakeVars::CMakeVarProvider& var_provider,
                                      const SourceControlFile& source_control_file,
                                      PackageSpec spec)
    {
        const auto& supports_expression = source_control_file.core_paragraph->supports_expression;
        if (supports_expression.is_empty())
        {
            return true;
        }
        PlatformExpression::Context context = var_provider.get_dep_info_vars(spec).value_or_exit(VCPKG_LINE_INFO);
        return supports_expression.evaluate(context);
    }

    static bool supported_for_triplet(const CMakeVars::CMakeVarProvider& var_provider,
                                      const InstallPlanAction* install_plan)
    {
        auto&& scfl = install_plan->source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
        return supported_for_triplet(var_provider, *scfl.source_control_file, install_plan->spec);
    }

    static bool supported_for_triplet(const CMakeVars::CMakeVarProvider& var_provider,
                                      const PortFileProvider& provider,
                                      PackageSpec spec)
    {
        auto&& scf = provider.get_control_file(spec.name()).value_or_exit(VCPKG_LINE_INFO).source_control_file;
        return supported_for_triplet(var_provider, *scf, spec);
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
            if (scfl.source_control_file->has_qualified_dependencies() ||
                !scfl.source_control_file->core_paragraph->supports_expression.is_empty())
            {
                packages_with_qualified_deps.push_back(spec.package_spec);
            }
        }

        var_provider.load_dep_info_vars(packages_with_qualified_deps, serialize_options.host_triplet);

        const auto applicable_specs = Util::filter(specs, [&](auto& spec) -> bool {
            return create_feature_install_plan(provider, var_provider, {&spec, 1}, {}, serialize_options)
                .unsupported_features.empty();
        });

        auto action_plan = create_feature_install_plan(provider, var_provider, applicable_specs, {}, serialize_options);
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
            if (it_parent == parent_hashes.end())
            {
                it->request_type = RequestType::USER_REQUESTED;
                if (it_known == known.end())
                {
                    to_keep.insert(it->spec);
                }
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

    static void print_regressions(const std::vector<SpecSummary>& results,
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
            auto msg = format_ci_result(r.get_spec(),
                                        result,
                                        cidata,
                                        ci_baseline_file_name,
                                        allow_unexpected_passing,
                                        !r.is_user_requested_install());
            if (!msg.empty())
            {
                has_error = true;
                output.append(msg).append_raw('\n');
            }
        }
        for (auto&& r : known)
        {
            auto msg =
                format_ci_result(r.first, r.second, cidata, ci_baseline_file_name, allow_unexpected_passing, true);
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

        print_default_triplet_warning(args);

        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);
        const auto& settings = options.settings;

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

        const IBuildLogsRecorder& build_logs_recorder =
            build_logs_recorder_storage ? *(build_logs_recorder_storage.get()) : null_build_logs_recorder();

        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(
            filesystem, *registry_set, make_overlay_provider(filesystem, paths.original_cwd, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        const ElapsedTimer timer;
        // Install the default features for every package
        std::vector<FullPackageSpec> all_default_full_specs;
        for (auto scfl : provider.load_all_control_files())
        {
            all_default_full_specs.emplace_back(
                PackageSpec{scfl->source_control_file->core_paragraph->name, target_triplet},
                InternalFeatureSet{"core", "default"});
        }

        CreateInstallPlanOptions serialize_options(host_triplet, paths.packages(), UnsupportedPortAction::Warn);

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
        BinaryCache binary_cache(args, paths, VCPKG_LINE_INFO);
        const auto precheck_results = binary_cache.precheck(action_plan.install_actions);
        auto split_specs =
            compute_action_statuses(ExclusionPredicate{&exclusions_map}, var_provider, precheck_results, action_plan);

        {
            std::string msg;
            for (const auto& spec : all_default_full_specs)
            {
                if (!Util::Sets::contains(split_specs->abi_map, spec.package_spec))
                {
                    bool supp = supported_for_triplet(var_provider, provider, spec.package_spec);
                    split_specs->known.emplace(spec.package_spec,
                                               supp ? BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES
                                                    : BuildResult::EXCLUDED);
                    msg += fmt::format("{:>40}: {:>8}\n", spec.package_spec, supp ? "cascade" : "skip");
                }
            }
            for (size_t i = 0; i < action_plan.install_actions.size(); ++i)
            {
                auto&& action = action_plan.install_actions[i];
                msg += fmt::format("{:>40}: {:>8}: {}\n",
                                   action.spec,
                                   split_specs->action_state_string[i],
                                   action.abi_info.value_or_exit(VCPKG_LINE_INFO).package_abi);
            }

            msg::write_unlocalized_text_to_stdout(Color::none, msg);
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
            auto parsed_json = Json::parse_file(VCPKG_LINE_INFO, filesystem, parent_hashes_path).value;
            parent_hashes = Util::fmap(parsed_json.array(VCPKG_LINE_INFO), [](const auto& json_object) {
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

        if (is_dry_run)
        {
            print_plan(action_plan, true, paths.builtin_ports_directory());
        }
        else
        {
            StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());
            auto already_installed = SetInstalled::adjust_action_plan_to_status_db(action_plan, status_db);
            Util::erase_if(already_installed,
                           [&](auto& spec) { return Util::Sets::contains(split_specs->known, spec); });
            if (!already_installed.empty())
            {
                msg::println_warning(msgCISkipInstallation, msg::list = Strings::join(", ", already_installed));
            }
            binary_cache.fetch(action_plan.install_actions);
            auto summary = Install::execute_plan(
                args, action_plan, KeepGoing::YES, paths, status_db, binary_cache, build_logs_recorder);

            for (auto&& result : summary.results)
            {
                split_specs->known.erase(result.get_spec());
            }

            msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("\nTriplet: {}\n", target_triplet));
            summary.print();
            print_regressions(
                summary.results, split_specs->known, cidata, baseline_iter->second, allow_unexpected_passing);

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
}
