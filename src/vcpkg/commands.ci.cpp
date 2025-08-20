#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/graphs.h>
#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/ci-baseline.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.ci.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/commands.set-installed.h>
#include <vcpkg/dependencies.h>
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
    constexpr CommandSetting CI_SETTINGS[] = {
        {SwitchExclude, msgCISettingsOptExclude},
        {SwitchHostExclude, msgCISettingsOptHostExclude},
        {SwitchXXUnit, msgCISettingsOptXUnit},
        {SwitchCIBaseline, msgCISettingsOptCIBase},
        {SwitchFailureLogs, msgCISettingsOptFailureLogs},
        {SwitchOutputHashes, msgCISettingsOptOutputHashes},
        {SwitchParentHashes, msgCISettingsOptParentHashes},
        {SwitchKnownFailuresFrom, msgCISettingsOptKnownFailuresFrom},
    };

    constexpr CommandSwitch CI_SWITCHES[] = {
        {SwitchDryRun, msgCISwitchOptDryRun},
        {SwitchXRandomize, msgCISwitchOptRandomize},
        {SwitchAllowUnexpectedPassing, msgCISwitchOptAllowUnexpectedPassing},
        {SwitchSkipFailures, msgCISwitchOptSkipFailures},
        {SwitchXXUnitAll, msgCISwitchOptXUnitAll},
    };

    struct UnknownCIPortsResults
    {
        std::map<PackageSpec, BuildResult> known;
        std::map<PackageSpec, std::vector<std::string>> features;
        std::map<PackageSpec, std::string> abi_map;
        // action_state_string.size() will equal install_actions.size()
        std::vector<StringLiteral> action_state_string;
    };

    bool supported_for_triplet(const CMakeVars::CMakeVarProvider& var_provider,
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

    bool supported_for_triplet(const CMakeVars::CMakeVarProvider& var_provider,
                               const PortFileProvider& provider,
                               PackageSpec spec)
    {
        auto&& scf = provider.get_control_file(spec.name()).value_or_exit(VCPKG_LINE_INFO).source_control_file;
        return supported_for_triplet(var_provider, *scf, spec);
    }

    ActionPlan compute_full_plan(const VcpkgPaths& paths,
                                 const PortFileProvider& provider,
                                 const CMakeVars::CMakeVarProvider& var_provider,
                                 const std::vector<FullPackageSpec>& specs,
                                 PackagesDirAssigner& packages_dir_assigner,
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
            PackagesDirAssigner this_packages_dir_not_used{""};
            return create_feature_install_plan(
                       provider, var_provider, {&spec, 1}, {}, this_packages_dir_not_used, serialize_options)
                .unsupported_features.empty();
        });

        auto action_plan = create_feature_install_plan(
            provider, var_provider, applicable_specs, {}, packages_dir_assigner, serialize_options);
        var_provider.load_tag_vars(action_plan, serialize_options.host_triplet);

        Checks::check_exit(VCPKG_LINE_INFO, action_plan.already_installed.empty());
        Checks::check_exit(VCPKG_LINE_INFO, action_plan.remove_actions.empty());

        compute_all_abis(paths, action_plan, var_provider, StatusParagraphs{});
        return action_plan;
    }

    std::unique_ptr<UnknownCIPortsResults> compute_action_statuses(
        ExclusionPredicate is_excluded,
        const std::vector<CacheAvailability>& precheck_results,
        const std::unordered_set<std::string>& known_failures,
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
                ret->known.emplace(p->spec, BuildResult::Excluded);
                will_fail.emplace(p->spec);
            }
            else if (Util::Sets::contains(known_failures, p->public_abi()))
            {
                ret->action_state_string.emplace_back("will fail");
                ret->known.emplace(p->spec, BuildResult::BuildFailed);
                will_fail.emplace(p->spec);
            }
            else if (Util::any_of(p->package_dependencies,
                                  [&](const PackageSpec& spec) { return Util::Sets::contains(will_fail, spec); }))
            {
                ret->action_state_string.emplace_back("cascade");
                ret->known.emplace(p->spec, BuildResult::CascadedDueToMissingDependencies);
                will_fail.emplace(p->spec);
            }
            else if (precheck_results[action_idx] == CacheAvailability::available)
            {
                ret->action_state_string.emplace_back("pass");
                ret->known.emplace(p->spec, BuildResult::Succeeded);
            }
            else
            {
                ret->action_state_string.emplace_back("*");
            }
        }
        return ret;
    }

    // This algorithm reduces an action plan to only unknown actions and their dependencies
    void reduce_action_plan(ActionPlan& action_plan,
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
                if (it_known != known.end() && it_known->second == BuildResult::Excluded)
                {
                    it->plan_type = InstallPlanType::EXCLUDED;
                }
                else
                {
                    to_keep.insert(it->package_dependencies.begin(), it->package_dependencies.end());
                }
            }
        }

        Util::erase_remove_if(action_plan.install_actions, [&to_keep](const InstallPlanAction& action) {
            return !Util::Sets::contains(to_keep, action.spec);
        });
    }

    void parse_exclusions(const std::map<StringLiteral, std::string, std::less<>>& settings,
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

    bool print_regressions(const std::vector<SpecSummary>& results,
                           const std::map<PackageSpec, BuildResult>& known,
                           const CiBaselineData& cidata,
                           const std::string* ci_baseline_file_name,
                           const LocalizedString& not_supported_regressions,
                           bool allow_unexpected_passing)
    {
        bool has_error = !not_supported_regressions.empty();
        LocalizedString output = msg::format(msgCiBaselineRegressionHeader);
        output.append_raw('\n');
        output.append(not_supported_regressions);
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

        if (has_error)
        {
            msg::write_unlocalized_text_to_stderr(Color::none, output);
        }

        return has_error;
    }

} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandCiMetadata{
        "ci",
        msgCmdCiSynopsis,
        {"vcpkg ci --triplet=x64-windows"},
        Undocumented,
        AutocompletePriority::Internal,
        0,
        0,
        {CI_SWITCHES, CI_SETTINGS},
        nullptr,
    };

    void command_ci_and_exit(const VcpkgCmdArguments& args,
                             const VcpkgPaths& paths,
                             Triplet target_triplet,
                             Triplet host_triplet)
    {
        msg::println_warning(msgInternalCICommand);
        const ParsedArguments options = args.parse_arguments(CommandCiMetadata);
        auto& fs = paths.get_filesystem();
        const auto& settings = options.settings;

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

        ExclusionsMap exclusions_map;
        parse_exclusions(settings, SwitchExclude, target_triplet, exclusions_map);
        parse_exclusions(settings, SwitchHostExclude, host_triplet, exclusions_map);
        auto baseline_iter = settings.find(SwitchCIBaseline);
        const std::string* ci_baseline_file_name = nullptr;
        const bool allow_unexpected_passing = Util::Sets::contains(options.switches, SwitchAllowUnexpectedPassing);
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
                Util::Sets::contains(options.switches, SwitchSkipFailures) ? SkipFailures::Yes : SkipFailures::No;
            ci_baseline_file_name = &baseline_iter->second;
            const auto ci_baseline_file_contents = fs.read_contents(*ci_baseline_file_name, VCPKG_LINE_INFO);
            ParseMessages ci_parse_messages;
            const auto lines = parse_ci_baseline(ci_baseline_file_contents, *ci_baseline_file_name, ci_parse_messages);
            ci_parse_messages.exit_if_errors_or_warnings();
            cidata = parse_and_apply_ci_baseline(lines, exclusions_map, skip_failures);
        }

        std::unordered_set<std::string> known_failures;
        auto it_known_failures = settings.find(SwitchKnownFailuresFrom);
        if (it_known_failures != settings.end())
        {
            Path raw_path = it_known_failures->second;
            auto lines = paths.get_filesystem().read_lines(raw_path).value_or_exit(VCPKG_LINE_INFO);
            known_failures.insert(lines.begin(), lines.end());
        }

        const auto is_dry_run = Util::Sets::contains(options.switches, SwitchDryRun);

        const IBuildLogsRecorder* build_logs_recorder = &null_build_logs_recorder;
        Optional<CiBuildLogsRecorder> build_logs_recorder_storage;
        {
            auto it_failure_logs = settings.find(SwitchFailureLogs);
            if (it_failure_logs != settings.end())
            {
                msg::println(msgCreateFailureLogsDir, msg::path = it_failure_logs->second);
                Path raw_path = it_failure_logs->second;
                fs.create_directories(raw_path, VCPKG_LINE_INFO);
                build_logs_recorder = &(build_logs_recorder_storage.emplace(
                    fs.almost_canonical(raw_path, VCPKG_LINE_INFO), fs.file_time_now()));
            }
        }

        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set, make_overlay_provider(fs, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        const ElapsedTimer timer;
        // Install the default features for every package
        std::vector<FullPackageSpec> all_default_full_specs;
        for (auto scfl : provider.load_all_control_files())
        {
            all_default_full_specs.emplace_back(
                PackageSpec{scfl->to_name(), target_triplet},
                InternalFeatureSet{FeatureNameCore.to_string(), FeatureNameDefault.to_string()});
        }

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
        GraphRandomizer* randomizer = nullptr;
        if (Util::Sets::contains(options.switches, SwitchXRandomize))
        {
            randomizer = &randomizer_instance;
        }

        PackagesDirAssigner packages_dir_assigner{paths.packages()};
        CreateInstallPlanOptions create_install_plan_options(
            randomizer, host_triplet, UnsupportedPortAction::Warn, UseHeadVersion::No, Editable::No);
        auto action_plan = compute_full_plan(
            paths, provider, var_provider, all_default_full_specs, packages_dir_assigner, create_install_plan_options);
        BinaryCache binary_cache(fs);
        if (!binary_cache.install_providers(args, paths, out_sink))
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        auto install_actions = Util::fmap(action_plan.install_actions, [](const auto& action) { return &action; });
        const auto precheck_results = binary_cache.precheck(install_actions);
        auto split_specs =
            compute_action_statuses(ExclusionPredicate{&exclusions_map}, precheck_results, known_failures, action_plan);
        LocalizedString not_supported_regressions;
        {
            std::string msg;
            for (const auto& spec : all_default_full_specs)
            {
                if (!Util::Sets::contains(split_specs->abi_map, spec.package_spec))
                {
                    bool supp = supported_for_triplet(var_provider, provider, spec.package_spec);
                    split_specs->known.emplace(spec.package_spec,
                                               supp ? BuildResult::CascadedDueToMissingDependencies
                                                    : BuildResult::Excluded);

                    if (cidata.expected_failures.contains(spec.package_spec))
                    {
                        not_supported_regressions
                            .append(supp ? msgCiBaselineUnexpectedFailCascade : msgCiBaselineUnexpectedFail,
                                    msg::spec = spec.package_spec,
                                    msg::triplet = spec.package_spec.triplet())
                            .append_raw('\n');
                    }
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

            msg::write_unlocalized_text(Color::none, msg);
            auto it_output_hashes = settings.find(SwitchOutputHashes);
            if (it_output_hashes != settings.end())
            {
                const Path output_hash_json = paths.original_cwd / it_output_hashes->second;
                Json::Array arr;
                for (size_t i = 0; i < action_plan.install_actions.size(); ++i)
                {
                    auto&& action = action_plan.install_actions[i];
                    Json::Object obj;
                    obj.insert(JsonIdName, Json::Value::string(action.spec.name()));
                    obj.insert(JsonIdTriplet, Json::Value::string(action.spec.triplet().canonical_name()));
                    obj.insert(JsonIdState, Json::Value::string(split_specs->action_state_string[i]));
                    obj.insert(JsonIdAbi,
                               Json::Value::string(action.abi_info.value_or_exit(VCPKG_LINE_INFO).package_abi));
                    arr.push_back(std::move(obj));
                }
                fs.write_contents(output_hash_json, Json::stringify(arr), VCPKG_LINE_INFO);
            }
        }

        std::vector<std::string> parent_hashes;

        auto it_parent_hashes = settings.find(SwitchParentHashes);
        if (it_parent_hashes != settings.end())
        {
            const Path parent_hashes_path = paths.original_cwd / it_parent_hashes->second;
            auto parsed_json = Json::parse_file(VCPKG_LINE_INFO, fs, parent_hashes_path).value;
            parent_hashes = Util::fmap(parsed_json.array(VCPKG_LINE_INFO), [](const auto& json_object) {
                auto abi = json_object.object(VCPKG_LINE_INFO).get(JsonIdAbi);
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
            print_plan(action_plan);
            if (!not_supported_regressions.empty())
            {
                msg::write_unlocalized_text_to_stderr(
                    Color::error,
                    msg::format(msgCiBaselineRegressionHeader).append_raw('\n').append_raw(not_supported_regressions));
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }
        else
        {
            StatusParagraphs status_db = database_load_collapse(fs, paths.installed());
            auto already_installed = adjust_action_plan_to_status_db(action_plan, status_db);
            Util::erase_if(already_installed,
                           [&](auto& spec) { return Util::Sets::contains(split_specs->known, spec); });
            if (!already_installed.empty())
            {
                LocalizedString warning;
                warning.append(msgCISkipInstallation);
                warning.append_floating_list(1, Util::fmap(already_installed, [](const PackageSpec& spec) {
                                                 return LocalizedString::from_raw(spec.to_string());
                                             }));
                msg::println_warning(warning);
            }

            install_preclear_plan_packages(paths, action_plan);
            binary_cache.fetch(action_plan.install_actions);

            auto summary = install_execute_plan(
                args, paths, host_triplet, build_options, action_plan, status_db, binary_cache, *build_logs_recorder);
            msg::println(msgTotalInstallTime, msg::elapsed = summary.elapsed);
            for (auto&& result : summary.results)
            {
                split_specs->known.erase(result.get_spec());
            }

            msg::print(LocalizedString::from_raw("\n")
                           .append(msgTripletLabel)
                           .append_raw(' ')
                           .append_raw(target_triplet)
                           .append_raw('\n')
                           .append(summary.format_results()));
            const bool any_regressions = print_regressions(summary.results,
                                                           split_specs->known,
                                                           cidata,
                                                           ci_baseline_file_name,
                                                           not_supported_regressions,
                                                           allow_unexpected_passing);

            auto it_xunit = settings.find(SwitchXXUnit);
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
                if (Util::Sets::contains(options.switches, SwitchXXUnitAll))
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

                fs.write_contents(it_xunit->second, xunitTestResults.build_xml(target_triplet), VCPKG_LINE_INFO);
            }

            if (any_regressions)
            {
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }
        binary_cache.wait_for_async_complete_and_join();
        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
