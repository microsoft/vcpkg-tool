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

    struct CIPreBuildStatus
    {
        std::map<PackageSpec, BuildResult> known;
        std::map<PackageSpec, std::string> report_lines;
        Json::Array abis;
    };

    enum class ExcludeReason
    {
        Baseline,
        Supports,
        Cascade
    };

    struct CiSpecsResult
    {
        std::vector<FullPackageSpec> requested;
        std::map<PackageSpec, ExcludeReason> excluded;
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

        return supports_expression.evaluate(var_provider.get_dep_info_vars(spec).value_or_exit(VCPKG_LINE_INFO));
    }

    bool cascade_for_triplet(const std::vector<InstallPlanAction>& install_actions,
                             const Triplet& target_triplet,
                             const SortedVector<std::string>* target_triplet_exclusions,
                             const Triplet& host_triplet,
                             const SortedVector<std::string>* host_triplet_exclusions)
    {
        return std::any_of(install_actions.begin(), install_actions.end(), [&](const InstallPlanAction& action) {
            if (target_triplet_exclusions && action.spec.triplet() == target_triplet)
                return target_triplet_exclusions->contains(action.spec.name());
            if (host_triplet_exclusions && action.spec.triplet() == host_triplet)
                return host_triplet_exclusions->contains(action.spec.name());
            return false;
        });
    }

    const SortedVector<std::string>* find_triplet_exclusions(const ExclusionsMap& exclusions_map,
                                                             const Triplet& triplet)
    {
        auto it = Util::find_if(exclusions_map.triplets, [&triplet](const TripletExclusions& exclusions) {
            return exclusions.triplet == triplet;
        });
        return it == exclusions_map.triplets.end() ? nullptr : &it->exclusions;
    }

    ActionPlan compute_full_plan(const VcpkgPaths& paths,
                                 const PortFileProvider& provider,
                                 const CMakeVars::CMakeVarProvider& var_provider,
                                 const std::vector<FullPackageSpec>& applicable_specs,
                                 PackagesDirAssigner& packages_dir_assigner,
                                 const CreateInstallPlanOptions& serialize_options)
    {
        StatusParagraphs empty_status_db;
        auto action_plan = create_feature_install_plan(
            provider, var_provider, applicable_specs, empty_status_db, packages_dir_assigner, serialize_options);
        var_provider.load_tag_vars(action_plan, serialize_options.host_triplet);

        Checks::check_exit(VCPKG_LINE_INFO, action_plan.already_installed.empty());
        Checks::check_exit(VCPKG_LINE_INFO, action_plan.remove_actions.empty());

        compute_all_abis(paths, action_plan, var_provider, empty_status_db);
        return action_plan;
    }

    CIPreBuildStatus compute_pre_build_statuses(const CiSpecsResult& ci_specs,
                                                const std::vector<CacheAvailability>& precheck_results,
                                                const std::unordered_set<std::string>& known_failure_abis,
                                                const std::unordered_set<std::string>& parent_hashes,
                                                const ActionPlan& action_plan)
    {
        static constexpr StringLiteral STATE_ABI_FAIL = "fail";
        static constexpr StringLiteral STATE_UNSUPPORTED = "unsupported";
        static constexpr StringLiteral STATE_CACHED = "cached";
        static constexpr StringLiteral STATE_PARENT = "parent";
        static constexpr StringLiteral STATE_UNKNOWN = "*";
        static constexpr StringLiteral STATE_SKIP = "skip";
        static constexpr StringLiteral STATE_CASCADE = "cascade";

        CIPreBuildStatus ret;
        std::unordered_set<PackageSpec> missing_specs;
        for (const FullPackageSpec& spec : ci_specs.requested)
        {
            missing_specs.insert(spec.package_spec);
        }

        for (size_t action_idx = 0; action_idx < action_plan.install_actions.size(); ++action_idx)
        {
            const auto& action = action_plan.install_actions[action_idx];
            missing_specs.erase(action.spec); // note action.spec won't be in missing_specs if it's a host dependency
            const std::string& public_abi = action.package_abi_or_exit(VCPKG_LINE_INFO);
            const StringLiteral* state;
            BuildResult known_result;
            if (Util::Sets::contains(known_failure_abis, public_abi))
            {
                state = &STATE_ABI_FAIL;
                known_result = BuildResult::BuildFailed;
            }
            else if (precheck_results[action_idx] == CacheAvailability::available)
            {
                state = &STATE_CACHED;
                known_result = BuildResult::Cached;
            }
            else if (Util::Sets::contains(parent_hashes, public_abi))
            {
                state = &STATE_PARENT;
                known_result = BuildResult::ExcludedByParent;
            }
            else
            {
                state = &STATE_UNKNOWN;
                known_result = BuildResult::ExcludedByDryRun;
            }

            ret.report_lines.insert_or_assign(action.spec,
                                              fmt::format("{:>40}: {:>6}: {}", action.spec, *state, public_abi));
            ret.known.emplace(action.spec, known_result);
            Json::Object obj;
            obj.insert(JsonIdName, Json::Value::string(action.spec.name()));
            obj.insert(JsonIdTriplet, Json::Value::string(action.spec.triplet().canonical_name()));
            obj.insert(JsonIdState, Json::Value::string(*state));
            obj.insert(JsonIdAbi, public_abi);
            ret.abis.push_back(std::move(obj));
        }

        if (!missing_specs.empty())
        {
            auto warning_text = msg::format(msgRequestedPortsNotInCIPlan);
            for (const PackageSpec& missing_spec : missing_specs)
            {
                warning_text.append_raw('\n');
                warning_text.append_raw(missing_spec.to_string());
            }

            console_diagnostic_context.report(DiagnosticLine{DiagKind::Warning, std::move(warning_text)});
        }

        for (const auto& exclusion : ci_specs.excluded)
        {
            const StringLiteral* state;
            BuildResult known_result;
            switch (exclusion.second)
            {
                // it probably makes sense to distinguish between "--exclude", "=skip" and "=fail but --skip-failures"
                // but we don't preserve that information right now, so all these cases report as "skip"
                case ExcludeReason::Baseline:
                    state = &STATE_SKIP;
                    known_result = BuildResult::Excluded;
                    break;
                case ExcludeReason::Supports:
                    state = &STATE_UNSUPPORTED;
                    known_result = BuildResult::Unsupported;
                    break;
                case ExcludeReason::Cascade:
                    state = &STATE_CASCADE;
                    known_result = BuildResult::CascadedDueToMissingDependencies;
                    break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }

            ret.report_lines.insert_or_assign(exclusion.first, fmt::format("{:>40}: {}", exclusion.first, *state));
            ret.known.insert_or_assign(exclusion.first, known_result);
        }

        return ret;
    }

    // This algorithm reduces an action plan to only unknown actions and their dependencies
    void prune_entirely_known_action_branches(ActionPlan& action_plan, const std::map<PackageSpec, BuildResult>& known)
    {
        std::set<PackageSpec> to_keep;
        for (auto it = action_plan.install_actions.rbegin(); it != action_plan.install_actions.rend(); ++it)
        {
            auto it_known = known.find(it->spec);
            if (it_known == known.end())
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }

            if (it_known->second != BuildResult::ExcludedByParent)
            {
                it->request_type = RequestType::USER_REQUESTED;
                if (it_known->second == BuildResult::ExcludedByDryRun)
                {
                    to_keep.insert(it->spec);
                }
            }

            if (Util::Sets::contains(to_keep, it->spec))
            {
                if (it_known->second != BuildResult::Excluded && it_known->second != BuildResult::Unsupported)
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

    bool print_regressions(const std::map<PackageSpec, CiResult>& ci_results,
                           const CiBaselineData& baseline_data,
                           const std::string* ci_baseline_file_name,
                           bool allow_unexpected_passing)
    {
        bool has_error = false;
        LocalizedString output = msg::format(msgCiBaselineRegressionHeader);
        output.append_raw('\n');
        for (auto&& ci_result : ci_results)
        {
            auto msg = format_ci_result(
                ci_result.first, ci_result.second.code, baseline_data, ci_baseline_file_name, allow_unexpected_passing);
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

    std::vector<PackageSpec> calculate_packages_with_qualifiers(
        const std::vector<const SourceControlFileAndLocation*>& all_control_files, const Triplet& target_triplet)
    {
        std::vector<PackageSpec> ret;
        for (auto scfl : all_control_files)
        {
            if (scfl->source_control_file->has_qualified_dependencies() ||
                !scfl->source_control_file->core_paragraph->supports_expression.is_empty())
            {
                ret.emplace_back(scfl->to_name(), target_triplet);
            }
        }

        return ret;
    }

    CiSpecsResult calculate_ci_specs(const ExclusionsMap& exclusions_map,
                                     const Triplet& target_triplet,
                                     const Triplet& host_triplet,
                                     PortFileProvider& provider,
                                     const CMakeVars::CMakeVarProvider& var_provider,
                                     const CreateInstallPlanOptions& serialize_options)
    {
        // Generate a spec for the default features for every package, except for those explicitly skipped.
        // While `reduce_action_plan` removes skipped packages as expected failures, there
        // it is too late as we have already calculated an action plan with feature dependencies from
        // the skipped ports.
        CiSpecsResult result;
        const SortedVector<std::string>* const target_triplet_exclusions =
            find_triplet_exclusions(exclusions_map, target_triplet);
        const SortedVector<std::string>* const host_triplet_exclusions =
            (host_triplet == target_triplet) ? nullptr : find_triplet_exclusions(exclusions_map, host_triplet);
        auto all_control_files = provider.load_all_control_files();

        // populate `var_provider` to evaluate supports expressions for all ports:
        std::vector<PackageSpec> packages_with_qualified_deps =
            calculate_packages_with_qualifiers(all_control_files, target_triplet);
        var_provider.load_dep_info_vars(packages_with_qualified_deps, serialize_options.host_triplet);

        for (auto scfl : all_control_files)
        {
            auto full_package_spec =
                FullPackageSpec{PackageSpec{scfl->to_name(), target_triplet},
                                InternalFeatureSet{FeatureNameCore.to_string(), FeatureNameDefault.to_string()}};
            if (target_triplet_exclusions && target_triplet_exclusions->contains(scfl->to_name()))
            {
                result.excluded.insert_or_assign(std::move(full_package_spec.package_spec), ExcludeReason::Baseline);
                continue;
            }

            PackagesDirAssigner this_packages_dir_not_used{""};
            const ActionPlan action_plan = create_feature_install_plan(
                provider, var_provider, {&full_package_spec, 1}, {}, this_packages_dir_not_used, serialize_options);
            if (!action_plan.unsupported_features.empty())
            {
                result.excluded.insert_or_assign(
                    std::move(full_package_spec.package_spec),
                    supported_for_triplet(var_provider, *scfl->source_control_file, full_package_spec.package_spec)
                        ? ExcludeReason::Cascade
                        : ExcludeReason::Supports);
                continue;
            }

            if (cascade_for_triplet(action_plan.install_actions,
                                    target_triplet,
                                    target_triplet_exclusions,
                                    host_triplet,
                                    host_triplet_exclusions))
            {
                result.excluded.insert_or_assign(std::move(full_package_spec.package_spec), ExcludeReason::Cascade);
                continue;
            }

            result.requested.emplace_back(std::move(full_package_spec));
        }

        return result;
    }

    struct CiRandomizer final : GraphRandomizer
    {
        virtual int random(int i) override
        {
            if (i <= 1) return 0;
            std::uniform_int_distribution<int> d(0, i - 1);
            return d(e);
        }

        std::random_device e;
    };

    std::unordered_set<std::string> parse_parent_hashes(
        const std::map<vcpkg::StringLiteral, std::string, std::less<void>>& settings, const VcpkgPaths& paths)
    {
        std::unordered_set<std::string> parent_hashes;
        const auto& fs = paths.get_filesystem();
        auto it_parent_hashes = settings.find(SwitchParentHashes);
        if (it_parent_hashes != settings.end())
        {
            const Path parent_hashes_path = paths.original_cwd / it_parent_hashes->second;
            auto parent_hashes_text = fs.try_read_contents(parent_hashes_path).value_or_exit(VCPKG_LINE_INFO);
            const auto parsed_object =
                Json::parse(parent_hashes_text.content, parent_hashes_text.origin).value_or_exit(VCPKG_LINE_INFO);
            const auto& parent_hashes_array = parsed_object.value.array(VCPKG_LINE_INFO);
            for (const Json::Value& array_value : parent_hashes_array)
            {
                auto abi = array_value.object(VCPKG_LINE_INFO).get(JsonIdAbi);
                Checks::check_exit(VCPKG_LINE_INFO, abi);
                parent_hashes.insert(abi->string(VCPKG_LINE_INFO).to_string());
            }
        }

        return parent_hashes;
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
        CiBaselineData baseline_data;
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
            baseline_data = parse_and_apply_ci_baseline(lines, exclusions_map, skip_failures);
        }

        std::unordered_set<std::string> known_failure_abis;
        auto it_known_failures = settings.find(SwitchKnownFailuresFrom);
        if (it_known_failures != settings.end())
        {
            Path raw_path = it_known_failures->second;
            auto lines = paths.get_filesystem().read_lines(raw_path).value_or_exit(VCPKG_LINE_INFO);
            known_failure_abis.insert(std::make_move_iterator(lines.begin()), std::make_move_iterator(lines.end()));
        }

        const std::unordered_set<std::string> parent_hashes = parse_parent_hashes(settings, paths);
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

        Optional<CiRandomizer> randomizer;
        if (Util::Sets::contains(options.switches, SwitchXRandomize))
        {
            randomizer.emplace();
        }
        CreateInstallPlanOptions create_install_plan_options(
            randomizer.get(), host_triplet, UnsupportedPortAction::Warn, UseHeadVersion::No, Editable::No);
        auto ci_specs = calculate_ci_specs(
            exclusions_map, target_triplet, host_triplet, provider, var_provider, create_install_plan_options);

        PackagesDirAssigner packages_dir_assigner{paths.packages()};
        auto action_plan = compute_full_plan(
            paths, provider, var_provider, ci_specs.requested, packages_dir_assigner, create_install_plan_options);
        BinaryCache binary_cache(fs);
        if (!binary_cache.install_providers(console_diagnostic_context, args, paths))
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        auto install_actions =
            Util::fmap(action_plan.install_actions, [](const InstallPlanAction& action) { return &action; });
        const auto precheck_results = binary_cache.precheck(console_diagnostic_context, fs, install_actions);
        const auto pre_build_status =
            compute_pre_build_statuses(ci_specs, precheck_results, known_failure_abis, parent_hashes, action_plan);
        {
            std::string msg;
            for (auto&& line : pre_build_status.report_lines)
            {
                msg += fmt::format("{}\n", line.second);
            }
            msg::write_unlocalized_text(Color::none, msg);
        }

        auto it_output_hashes = settings.find(SwitchOutputHashes);
        if (it_output_hashes != settings.end())
        {
            const Path output_hash_json = paths.original_cwd / it_output_hashes->second;
            fs.write_contents(output_hash_json, Json::stringify(pre_build_status.abis), VCPKG_LINE_INFO);
        }

        prune_entirely_known_action_branches(action_plan, pre_build_status.known);

        msg::println(msgElapsedTimeForChecks, msg::elapsed = timer.elapsed());
        std::map<PackageSpec, CiResult> ci_plan_results;
        std::map<PackageSpec, CiResult> ci_full_results;
        for (auto&& pre_known_outcome : pre_build_status.known)
        {
            ci_full_results.insert_or_assign(pre_known_outcome.first, CiResult{pre_known_outcome.second, nullopt});
        }

        if (is_dry_run)
        {
            print_plan(action_plan);
        }
        else
        {
            StatusParagraphs status_db = database_load_collapse(fs, paths.installed());
            auto already_installed = adjust_action_plan_to_status_db(action_plan, status_db);
            Util::erase_if(already_installed,
                           [&](const PackageSpec& spec) { return Util::Sets::contains(pre_build_status.known, spec); });
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
            binary_cache.fetch(console_diagnostic_context, fs, action_plan.install_actions);

            auto summary = install_execute_plan(
                args, paths, host_triplet, build_options, action_plan, status_db, binary_cache, *build_logs_recorder);
            msg::println(msgTotalInstallTime, msg::elapsed = summary.elapsed);

            for (auto&& result : summary.results)
            {
                if (const auto* ipa = result.get_maybe_install_plan_action())
                {
                    // note that we assign over the 'known' values from above
                    auto ci_result = CiResult{result.build_result.value_or_exit(VCPKG_LINE_INFO).code,
                                              CiBuiltResult{ipa->package_abi_or_exit(VCPKG_LINE_INFO),
                                                            ipa->feature_list,
                                                            result.start_time,
                                                            result.timing}};
                    ci_plan_results.insert_or_assign(result.get_spec(), ci_result);
                    ci_full_results.insert_or_assign(result.get_spec(), std::move(ci_result));
                }
            }
        }

        binary_cache.wait_for_async_complete_and_join();
        msg::println();
        std::map<Triplet, BuildResultCounts> summary_counts;
        auto summary_report = msg::format(msgTripletLabel).data();
        summary_report.push_back(' ');
        target_triplet.to_string(summary_report);
        summary_report.push_back('\n');
        for (auto&& ci_result : ci_plan_results)
        {
            summary_report.append(2, ' ');
            ci_result.first.to_string(summary_report);
            summary_report.append(": ");
            ci_result.second.to_string(summary_report);
            summary_report.push_back('\n');
        }

        for (auto&& ci_result : ci_full_results)
        {
            summary_counts[ci_result.first.triplet()].increment(ci_result.second.code);
        }

        for (auto&& summary_count : summary_counts)
        {
            summary_report.push_back('\n');
            summary_report.append(summary_count.second.format(summary_count.first).data());
        }

        summary_report.push_back('\n');
        msg::println();
        msg::print(LocalizedString::from_raw(std::move(summary_report)));

        const bool any_regressions =
            print_regressions(ci_full_results, baseline_data, ci_baseline_file_name, allow_unexpected_passing);

        auto it_xunit = settings.find(SwitchXXUnit);
        if (it_xunit != settings.end())
        {
            XunitWriter xunitTestResults;
            const auto& xunit_results =
                Util::Sets::contains(options.switches, SwitchXXUnitAll) ? ci_full_results : ci_plan_results;
            for (auto&& xunit_result : xunit_results)
            {
                xunitTestResults.add_test_results(xunit_result.first, xunit_result.second);
            }

            fs.write_contents(it_xunit->second, xunitTestResults.build_xml(target_triplet), VCPKG_LINE_INFO);
        }

        if (any_regressions)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
