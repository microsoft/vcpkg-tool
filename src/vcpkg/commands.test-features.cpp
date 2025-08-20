#include <vcpkg/base/cache.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/fmt.h>
#include <vcpkg/base/git.h>
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
#include <vcpkg/tools.h>
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

    std::vector<std::string> get_for_merge_with_test_port_names(const VcpkgPaths& paths, StringView for_merge_with)
    {
        auto& fs = paths.get_filesystem();
        auto& builtin_ports = paths.builtin_ports_directory();
        auto git_exe = paths.get_tool_exe(Tools::GIT, out_sink);
        auto ports_dir_prefix =
            git_prefix(console_diagnostic_context, git_exe, builtin_ports).value_or_quiet_exit(VCPKG_LINE_INFO);
        const auto locator = GitRepoLocator{GitRepoLocatorKind::CurrentDirectory, builtin_ports};
        auto index_file =
            git_index_file(console_diagnostic_context, fs, git_exe, locator).value_or_quiet_exit(VCPKG_LINE_INFO);
        TempFileDeleter temp_index_file{fs, fmt::format("{}_vcpkg_{}.tmp", index_file.native(), get_process_id())};
        if (!fs.copy_file(
                console_diagnostic_context, index_file, temp_index_file.path, CopyOptions::overwrite_existing) ||
            !git_add_with_index(console_diagnostic_context, git_exe, builtin_ports, temp_index_file.path))
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        auto head_tree = git_write_index_tree(console_diagnostic_context, git_exe, locator, temp_index_file.path)
                             .value_or_quiet_exit(VCPKG_LINE_INFO);
        auto merge_base = git_merge_base(console_diagnostic_context, git_exe, locator, for_merge_with, "HEAD")
                              .value_or_quiet_exit(VCPKG_LINE_INFO);
        auto diffs = git_diff_tree(console_diagnostic_context,
                                   git_exe,
                                   locator,
                                   fmt::format("{}:{}", merge_base, ports_dir_prefix),
                                   fmt::format("{}:{}", head_tree, ports_dir_prefix))
                         .value_or_quiet_exit(VCPKG_LINE_INFO);
        std::vector<std::string> test_port_names;
        for (auto&& diff : diffs)
        {
            switch (diff.kind)
            {
                case GitDiffTreeLineKind::Added:
                case GitDiffTreeLineKind::Copied:
                case GitDiffTreeLineKind::Modified:
                case GitDiffTreeLineKind::Renamed:
                case GitDiffTreeLineKind::TypeChange: test_port_names.push_back(std::move(diff.file_name)); break;
                case GitDiffTreeLineKind::Deleted:
                case GitDiffTreeLineKind::Unmerged:
                case GitDiffTreeLineKind::Unknown: break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        return test_port_names;
    }

    std::vector<SourceControlFile*> load_all_scf_by_name(View<std::string> test_port_names,
                                                         PathsPortFileProvider& provider)
    {
        return Util::fmap(test_port_names, [&](const std::string& arg) {
            return provider.get_control_file(arg).value_or_exit(VCPKG_LINE_INFO).source_control_file.get();
        });
    }

    enum class SpecToTestKind
    {
        Core,
        Separate,
        Combined
    };

    struct SpecToTest : FullPackageSpec
    {
        explicit SpecToTest(const PackageSpec& package_spec, InternalFeatureSet&& features, SpecToTestKind kind)
            : FullPackageSpec(package_spec, std::move(features)), plan(), kind(kind), separate_feature()
        {
        }

        explicit SpecToTest(const PackageSpec& package_spec, InternalFeatureSet&& features, const std::string& feature)
            : FullPackageSpec(package_spec, std::move(features))
            , plan()
            , kind(SpecToTestKind::Separate)
            , separate_feature(feature)
        {
        }

        FullPackageSpec non_core_spec() const
        {
            InternalFeatureSet non_core_features;
            Util::copy_if(
                non_core_features, features, [](const std::string& feature) { return feature != FeatureNameCore; });
            return FullPackageSpec(package_spec, std::move(non_core_features));
        }

        ActionPlan plan;

        SpecToTestKind kind;

        // If kind == SpecToTestKind::Separate, the name of the separately tested feature; otherwise, empty string
        std::string separate_feature;
    };
} // unnamed namespace

VCPKG_FORMAT_WITH_TO_STRING(SpecToTest);

namespace
{
    void add_build_cascade_diagnostic(std::vector<DiagnosticLine>& diagnostics,
                                      const FullPackageSpec& spec,
                                      const std::string* ci_feature_baseline_file_name,
                                      const SourceLoc& loc,
                                      std::string&& cascade_reason)
    {
        auto text =
            msg::format(msgUnexpectedStateCascade, msg::feature_spec = spec).append_raw(" ").append_raw(cascade_reason);
        if (ci_feature_baseline_file_name)
        {
            diagnostics.push_back(DiagnosticLine{
                DiagKind::Error, *ci_feature_baseline_file_name, TextRowCol{loc.row, loc.column}, std::move(text)});
        }
        else
        {
            diagnostics.push_back(DiagnosticLine{DiagKind::Error, std::move(text)});
        }
    }

    void handle_cascade_feature_test_result(std::vector<DiagnosticLine>& diagnostics,
                                            bool enforce_marked_cascades,
                                            const FullPackageSpec& spec,
                                            const std::string* ci_feature_baseline_file_name,
                                            const CiFeatureBaselineEntry* baseline,
                                            std::string&& cascade_reason)
    {
        auto outcome = expected_outcome(baseline, spec.features);
        switch (outcome.value)
        {
            case CiFeatureBaselineOutcome::ImplicitPass:
                if (!enforce_marked_cascades)
                {
                    break;
                }

                [[fallthrough]];
            case CiFeatureBaselineOutcome::ExplicitPass:
            case CiFeatureBaselineOutcome::ConfigurationFail:
                add_build_cascade_diagnostic(
                    diagnostics, spec, ci_feature_baseline_file_name, outcome.loc, std::move(cascade_reason));
                break;
            case CiFeatureBaselineOutcome::PortMarkedFail:
            case CiFeatureBaselineOutcome::FeatureFail:
                add_build_cascade_diagnostic(
                    diagnostics, spec, ci_feature_baseline_file_name, outcome.loc, std::move(cascade_reason));
                diagnostics.push_back(DiagnosticLine{DiagKind::Note,
                                                     *ci_feature_baseline_file_name,
                                                     TextRowCol{outcome.loc.row, outcome.loc.column},
                                                     msg::format(msgUnexpectedStateCascadePortNote)});
                break;
            case CiFeatureBaselineOutcome::PortMarkedCascade:
            case CiFeatureBaselineOutcome::FeatureCascade:
                // this is the expected outcome, nothing to do
                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    void add_build_failed_but_marked_cascade_diagnostic(std::vector<DiagnosticLine>& diagnostics,
                                                        const FullPackageSpec& spec,
                                                        StringView ci_feature_baseline_file_name,
                                                        const SourceLoc& loc)
    {
        diagnostics.push_back(DiagnosticLine{DiagKind::Error,
                                             ci_feature_baseline_file_name,
                                             TextRowCol{loc.row, loc.column},
                                             msg::format(msgUnexpectedStateFailedCascade, msg::feature_spec = spec)});
    }

    void handle_fail_feature_test_result(std::vector<DiagnosticLine>& diagnostics,
                                         const SpecToTest& spec,
                                         const std::string* ci_feature_baseline_file_name,
                                         const CiFeatureBaselineEntry* baseline)
    {
        auto outcome = expected_outcome(baseline, spec.features);
        switch (outcome.value)
        {
            case CiFeatureBaselineOutcome::ImplicitPass:
            case CiFeatureBaselineOutcome::ExplicitPass:
                if (ci_feature_baseline_file_name)
                {
                    diagnostics.push_back(
                        DiagnosticLine{DiagKind::Error,
                                       *ci_feature_baseline_file_name,
                                       msg::format(msgUnexpectedStateFailedPass, msg::feature_spec = spec)});
                    switch (spec.kind)
                    {
                        case SpecToTestKind::Core:
                            diagnostics.push_back(
                                DiagnosticLine{DiagKind::Note,
                                               msg::format(msgUnexpectedStateFailedNoteConsiderSkippingPort,
                                                           msg::package_name = spec.package_spec.name(),
                                                           msg::spec = spec.package_spec)});
                            break;
                        case SpecToTestKind::Separate:
                            diagnostics.push_back(
                                DiagnosticLine{DiagKind::Note,
                                               msg::format(msgUnexpectedStateFailedNoteSeparateCombinationFails,
                                                           msg::feature_spec = spec,
                                                           msg::feature = format_name_only_feature_spec(
                                                               spec.package_spec.name(), spec.separate_feature))});
                            diagnostics.push_back(DiagnosticLine{
                                DiagKind::Note,
                                msg::format(msgUnexpectedStateFailedNoteSeparateFeatureFails,
                                            msg::feature_spec = FullPackageSpec(
                                                spec.package_spec, InternalFeatureSet{{spec.separate_feature}}),
                                            msg::feature = format_name_only_feature_spec(spec.package_spec.name(),
                                                                                         spec.separate_feature))});

                            break;
                        case SpecToTestKind::Combined:
                            diagnostics.push_back(DiagnosticLine{
                                DiagKind::Note,
                                msg::format(msgUnexpectedStateFailedNoteConsiderSkippingPortOrCombination,
                                            msg::package_name = spec.package_spec.name(),
                                            msg::spec = spec.package_spec,
                                            msg::feature_spec = spec)});
                            break;
                        default: Checks::unreachable(VCPKG_LINE_INFO);
                    }

                    if (spec.kind != SpecToTestKind::Combined)
                    {
                        diagnostics.push_back(
                            DiagnosticLine{DiagKind::Note,
                                           msg::format(msgUnexpectedStateFailedNoteMoreFeaturesRequired,
                                                       msg::package_name = spec.package_spec.name())});
                    }
                }
                else
                {
                    diagnostics.push_back(DiagnosticLine{
                        DiagKind::Error, msg::format(msgUnexpectedStateFailedPass, msg::feature_spec = spec)});
                }
                break;
            case CiFeatureBaselineOutcome::PortMarkedCascade:
                add_build_failed_but_marked_cascade_diagnostic(
                    diagnostics, spec, *ci_feature_baseline_file_name, outcome.loc);
                diagnostics.push_back(
                    DiagnosticLine{DiagKind::Note, msg::format(msgUnexpectedStateFailedNotePortMarkedCascade)});
                break;
            case CiFeatureBaselineOutcome::FeatureCascade:
                add_build_failed_but_marked_cascade_diagnostic(
                    diagnostics, spec, *ci_feature_baseline_file_name, outcome.loc);
                diagnostics.push_back(
                    DiagnosticLine{DiagKind::Note, msg::format(msgUnexpectedStateFailedNoteFeatureMarkedCascade)});
                break;
            case CiFeatureBaselineOutcome::PortMarkedFail:
            case CiFeatureBaselineOutcome::FeatureFail:
            case CiFeatureBaselineOutcome::ConfigurationFail:
                // this is the expected outcome, nothing to do
                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    void add_build_pass_but_marked_diagnostic(msg::MessageT<msg::feature_spec_t> message,
                                              std::vector<DiagnosticLine>& diagnostics,
                                              const FullPackageSpec& spec,
                                              const std::string& ci_feature_baseline_file_name,
                                              const SourceLoc& loc)
    {
        diagnostics.push_back(DiagnosticLine{DiagKind::Error,
                                             ci_feature_baseline_file_name,
                                             TextRowCol{loc.row, loc.column},
                                             msg::format(message, msg::feature_spec = spec)});
    }

    void add_build_pass_but_feature_marked_diagnostics(
        msg::MessageT<msg::feature_spec_t, msg::feature_t> message,
        std::vector<DiagnosticLine>& diagnostics,
        const FullPackageSpec& spec,
        const std::string& ci_feature_baseline_file_name,
        const std::set<Located<std::string>, LocatedStringLess>& baseline_feature_set)
    {
        for (auto&& spec_feature : spec.features)
        {
            for (auto&& baseline_feature : baseline_feature_set)
            {
                if (spec_feature == baseline_feature.value)
                {
                    diagnostics.push_back(
                        DiagnosticLine{DiagKind::Error,
                                       ci_feature_baseline_file_name,
                                       TextRowCol{baseline_feature.loc.row, baseline_feature.loc.column},
                                       msg::format(message,
                                                   msg::feature_spec = spec,
                                                   msg::feature = format_name_only_feature_spec(
                                                       spec.package_spec.name(), baseline_feature.value))});
                }
            }
        }
    }

    void handle_pass_feature_test_result(std::vector<DiagnosticLine>& diagnostics,
                                         const FullPackageSpec& spec,
                                         const std::string* ci_feature_baseline_file_name,
                                         const CiFeatureBaselineEntry* baseline)
    {
        if (!baseline)
        {
            return;
        }

        if (auto pstate = baseline->state.get())
        {
            switch (pstate->value)
            {
                case CiFeatureBaselineState::Fail:
                    add_build_pass_but_marked_diagnostic(msgUnexpectedStatePassPortMarkedFail,
                                                         diagnostics,
                                                         spec,
                                                         *ci_feature_baseline_file_name,
                                                         pstate->loc);
                    break;
                case CiFeatureBaselineState::Cascade:
                    add_build_pass_but_marked_diagnostic(msgUnexpectedStatePassPortMarkedCascade,
                                                         diagnostics,
                                                         spec,
                                                         *ci_feature_baseline_file_name,
                                                         pstate->loc);
                    break;
                case CiFeatureBaselineState::Skip:
                case CiFeatureBaselineState::Pass: break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        for (auto&& failing_configuration : baseline->fail_configurations)
        {
            if (std::is_permutation(failing_configuration.value.begin(),
                                    failing_configuration.value.end(),
                                    spec.features.begin(),
                                    spec.features.end()))
            {
                add_build_pass_but_marked_diagnostic(msgUnexpectedStatePassPortMarkedFail,
                                                     diagnostics,
                                                     spec,
                                                     *ci_feature_baseline_file_name,
                                                     failing_configuration.loc);
            }
        }

        add_build_pass_but_feature_marked_diagnostics(msgUnexpectedStatePassFeatureMarkedFail,
                                                      diagnostics,
                                                      spec,
                                                      *ci_feature_baseline_file_name,
                                                      baseline->failing_features);
        add_build_pass_but_feature_marked_diagnostics(msgUnexpectedStatePassFeatureMarkedCascade,
                                                      diagnostics,
                                                      spec,
                                                      *ci_feature_baseline_file_name,
                                                      baseline->cascade_features);
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
        {SwitchForMergeWith, msgCmdOptForMergeWith},
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
        ParsedArguments options = args.parse_arguments(CommandTestFeaturesMetadata);
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

        auto it_merge_with = settings.find(SwitchForMergeWith);
        if (all_ports)
        {
            if (it_merge_with != settings.end())
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                            msgMutuallyExclusiveOption,
                                            msg::value = SwitchAll,
                                            msg::option = SwitchForMergeWith);
            }

            if (!options.command_arguments.empty())
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgMutuallyExclusivePorts, msg::option = SwitchAll);
            }

            feature_test_ports =
                Util::fmap(provider.load_all_control_files(),
                           [](const SourceControlFileAndLocation* scfl) { return scfl->source_control_file.get(); });
        }
        else if (it_merge_with == settings.end())
        {
            feature_test_ports = load_all_scf_by_name(options.command_arguments, provider);
        }
        else if (options.command_arguments.empty())
        {
            auto test_port_names = get_for_merge_with_test_port_names(paths, it_merge_with->second);
            msg::print(msg::format(msgForMergeWithTestingTheFollowing, msg::value = it_merge_with->second)
                           .append_raw(' ')
                           .append_raw(Strings::join(" ", test_port_names))
                           .append_raw('\n'));
            feature_test_ports = load_all_scf_by_name(test_port_names, provider);
        }
        else
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgMutuallyExclusivePorts, msg::option = SwitchForMergeWith);
        }

        // Note that the baseline file text needs to live as long as feature_baseline,
        // because there are pointers into it.
        auto feature_baseline_iter = settings.find(SwitchCIFeatureBaseline);
        std::string ci_feature_baseline_file_contents;
        const std::string* ci_feature_baseline_file_name = nullptr;
        CiFeatureBaseline feature_baseline;
        if (feature_baseline_iter != settings.end())
        {
            ci_feature_baseline_file_name = &feature_baseline_iter->second;
            ci_feature_baseline_file_contents = fs.read_contents(*ci_feature_baseline_file_name, VCPKG_LINE_INFO);
            ParseMessages ci_parse_messages;
            feature_baseline = parse_ci_feature_baseline(ci_feature_baseline_file_contents,
                                                         *ci_feature_baseline_file_name,
                                                         ci_parse_messages,
                                                         target_triplet,
                                                         host_triplet,
                                                         var_provider);
            ci_parse_messages.exit_if_errors_or_warnings();
        }

        // to reduce number of cmake invocations
        auto all_specs = Util::fmap(feature_test_ports, [&](const SourceControlFile* scf) {
            return PackageSpec(scf->core_paragraph->name, target_triplet);
        });
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
        std::vector<SpecToTest> specs_to_test;
        for (const auto port : feature_test_ports)
        {
            const auto* baseline = feature_baseline.get_port(port->core_paragraph->name);
            CiFeatureBaselineState expected_overall_state = CiFeatureBaselineState::Pass;
            if (baseline)
            {
                if (auto pstate = baseline->state.get())
                {
                    expected_overall_state = pstate->value;
                }
            }

            if (expected_overall_state == CiFeatureBaselineState::Skip) continue;
            PackageSpec package_spec(port->core_paragraph->name, target_triplet);
            const auto dep_info_vars = var_provider.get_or_load_dep_info_vars(package_spec, host_triplet);
            if (!port->core_paragraph->supports_expression.evaluate(dep_info_vars))
            {
                msg::println(
                    msgPortNotSupported, msg::package_name = port->core_paragraph->name, msg::triplet = target_triplet);
                continue;
            }

            if (test_feature_core && (!baseline || !Util::Sets::contains(baseline->skip_features, FeatureNameCore)))
            {
                auto& core_test = specs_to_test.emplace_back(
                    package_spec, InternalFeatureSet{{FeatureNameCore.to_string()}}, SpecToTestKind::Core);
                if (baseline)
                {
                    for (const auto& option_set : baseline->options)
                    {
                        if (option_set.value.front() != FeatureNameCore)
                        {
                            core_test.features.push_back(option_set.value.front());
                        }
                    }
                }
            }
            InternalFeatureSet combined_features{{FeatureNameCore.to_string()}};
            for (const auto& feature : port->feature_paragraphs)
            {
                if (!feature->supports_expression.evaluate(dep_info_vars))
                {
                    // skip unsupported features
                    continue;
                }

                if (baseline && Util::Sets::contains(baseline->skip_features, feature->name))
                {
                    // skip skipped features
                    continue;
                }

                // Add this feature to the combined features test.
                // Skip adding it if:
                // * It is expected to be a cascaded failure
                // * It is an expected failure
                // * It is not the first member of every option set in which it appears
                //   (That is, the combined features test always chooses the first option of each 'options set')
                if (test_features_combined &&
                    (!baseline ||
                     (!Util::Sets::contains(baseline->cascade_features, feature->name) &&
                      !Util::Sets::contains(baseline->failing_features, feature->name) &&
                      Util::all_of(baseline->options, [&](const Located<std::vector<std::string>>& option_set) {
                          return !Util::contains(option_set.value, feature->name) ||
                                 option_set.value.front() == feature->name ||
                                 (option_set.value.size() >= 2 && option_set.value[0] == FeatureNameCore &&
                                  option_set.value[1] == feature->name);
                      }))))
                {
                    combined_features.push_back(feature->name);
                }

                // Add the separate feature test.
                if (test_features_separately &&
                    (!baseline || !Util::Sets::contains(baseline->no_separate_feature_test, feature->name)))
                {
                    InternalFeatureSet separate_features{{FeatureNameCore.to_string(), feature->name}};
                    if (baseline)
                    {
                        // For each option set, we add the first option, unless this feature is in the option
                        // set (In which case, this feature is the selected one from that option set itself)
                        for (const auto& option_set : baseline->options)
                        {
                            if (option_set.value.front() != FeatureNameCore &&
                                !Util::contains(option_set.value, feature->name))
                            {
                                separate_features.push_back(option_set.value.front());
                            }
                        }
                    }

                    if (Util::none_of(specs_to_test, [&](const SpecToTest& spec) {
                            return std::is_permutation(spec.features.begin(),
                                                       spec.features.end(),
                                                       separate_features.begin(),
                                                       separate_features.end());
                        }))
                    {
                        specs_to_test.emplace_back(package_spec, std::move(separate_features), feature->name);
                    }
                }
            }

            if (test_features_combined && Util::none_of(specs_to_test, [&](const SpecToTest& test) {
                    return std::is_permutation(
                        test.features.begin(), test.features.end(), combined_features.begin(), combined_features.end());
                }))
            {
                specs_to_test.emplace_back(package_spec, std::move(combined_features), SpecToTestKind::Combined);
            }
        }

        msg::println(msgComputeInstallPlans, msg::count = specs_to_test.size());

        std::vector<FullPackageSpec> specs;
        std::vector<Path> port_locations;
        std::vector<const InstallPlanAction*> actions_to_check;
        for (auto&& test_spec : specs_to_test)
        {
            test_spec.plan = create_feature_install_plan(provider,
                                                         var_provider,
                                                         Span<FullPackageSpec>(&test_spec, 1),
                                                         {},
                                                         packages_dir_assigner,
                                                         install_plan_options);
            if (test_spec.plan.unsupported_features.empty())
            {
                for (auto& actions : test_spec.plan.install_actions)
                {
                    specs.emplace_back(actions.spec, actions.feature_list);
                    port_locations.emplace_back(
                        actions.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).port_directory());
                }
                actions_to_check.push_back(&test_spec.plan.install_actions.back());
            }
        }

        msg::println(msgComputeAllAbis);
        var_provider.load_tag_vars(specs, port_locations, host_triplet);
        for (auto&& test_spec : specs_to_test)
        {
            if (test_spec.plan.unsupported_features.empty())
            {
                compute_all_abis(paths, test_spec.plan, var_provider, status_db, port_dir_abi_info_cache);
            }
        }

        msg::println(msgPrecheckBinaryCache);
        binary_cache.precheck(actions_to_check);

        Util::stable_sort(specs_to_test, [](const SpecToTest& left, const SpecToTest& right) noexcept {
            return left.plan.install_actions.size() < right.plan.install_actions.size();
        });

        // test port features
        std::unordered_set<std::string> known_failures;
        std::vector<DiagnosticLine> diagnostics;

        for (std::size_t i = 0; i < specs_to_test.size(); ++i)
        {
            auto& spec = specs_to_test[i];
            auto& install_plan = spec.plan;
            msg::println(msgStartingFeatureTest,
                         msg::value = fmt::format("{}/{}", i + 1, specs_to_test.size()),
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
                handle_cascade_feature_test_result(
                    diagnostics, all_ports, spec, ci_feature_baseline_file_name, baseline, Strings::join(", ", out));
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
                handle_cascade_feature_test_result(
                    diagnostics, all_ports, spec, ci_feature_baseline_file_name, baseline, iter->display_name());
                continue;
            }

            // only install the absolute minimum
            adjust_action_plan_to_status_db(install_plan, status_db);
            if (install_plan.install_actions.empty()) // already installed
            {
                msg::println(msgAlreadyInstalled, msg::spec = spec);
                handle_pass_feature_test_result(diagnostics, spec, ci_feature_baseline_file_name, baseline);
                continue;
            }

            {
                const InstallPlanAction* action = &install_plan.install_actions.back();
                if (binary_cache.precheck(View<const InstallPlanAction*>(&action, 1)).front() ==
                    CacheAvailability::available)
                {
                    msg::println(msgSkipTestingOfPortAlreadyInBinaryCache,
                                 msg::sha = action->package_abi().value_or_exit(VCPKG_LINE_INFO));
                    handle_pass_feature_test_result(diagnostics, spec, ci_feature_baseline_file_name, baseline);
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
                build_logs_recorder = &(feature_build_logs_recorder_storage.emplace(logs_dir, fs.file_time_now()));
            }

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
            for (const auto& result : summary.results)
            {
                auto& build_result = result.build_result.value_or_exit(VCPKG_LINE_INFO);
                switch (build_result.code)
                {
                    case BuildResult::BuildFailed:
                        if (Path* logs_dir = maybe_logs_dir.get())
                        {
                            auto issue_body_path = *logs_dir / FileIssueBodyMD;
                            fs.write_contents(
                                issue_body_path,
                                create_github_issue(args,
                                                    build_result,
                                                    paths,
                                                    result.get_install_plan_action().value_or_exit(VCPKG_LINE_INFO),
                                                    false),
                                VCPKG_LINE_INFO);
                        }

                        [[fallthrough]];
                    case BuildResult::PostBuildChecksFailed:
                        known_failures.insert(result.get_abi().value_or_exit(VCPKG_LINE_INFO));
                        break;
                    default: break;
                }
            }

            auto& last_build_result = summary.results.back().build_result.value_or_exit(VCPKG_LINE_INFO);
            switch (last_build_result.code)
            {
                case BuildResult::Downloaded:
                case BuildResult::Succeeded:
                    handle_pass_feature_test_result(diagnostics, spec, ci_feature_baseline_file_name, baseline);
                    break;
                case BuildResult::CascadedDueToMissingDependencies:
                    if (last_build_result.unmet_dependencies.empty())
                    {
                        Checks::unreachable(VCPKG_LINE_INFO);
                    }

                    handle_cascade_feature_test_result(
                        diagnostics,
                        all_ports,
                        spec,
                        ci_feature_baseline_file_name,
                        baseline,
                        Strings::join(",",
                                      Util::fmap(last_build_result.unmet_dependencies,
                                                 [](const FullPackageSpec& unmet_dependency) {
                                                     return unmet_dependency.to_string();
                                                 })));
                    break;
                case BuildResult::BuildFailed:
                case BuildResult::PostBuildChecksFailed:
                case BuildResult::FileConflicts:
                case BuildResult::CacheMissing:
                    if (auto abi = summary.results.back().get_abi().get())
                    {
                        known_failures.insert(*abi);
                    }

                    if (maybe_logs_dir)
                    {
                        fs.create_directories(*maybe_logs_dir.get(), VCPKG_LINE_INFO);
                        fs.write_contents(
                            *maybe_logs_dir.get() / FileTestedSpecDotTxt, spec.to_string(), VCPKG_LINE_INFO);
                    }

                    handle_fail_feature_test_result(diagnostics, spec, ci_feature_baseline_file_name, baseline);
                    break;
                case BuildResult::Removed:
                case BuildResult::Excluded: Checks::unreachable(VCPKG_LINE_INFO);
            }

            msg::println();
        }

        int exit_code;
        if (diagnostics.empty())
        {
            exit_code = EXIT_SUCCESS;
            msg::println(msgAllFeatureTestsPassed);
        }
        else
        {
            exit_code = EXIT_FAILURE;
            msg::println(msgFeatureTestProblems);
            for (const auto& result : diagnostics)
            {
                result.print_to(out_sink);
            }
        }

        auto it_output_file = settings.find(SwitchFailingAbiLog);
        if (it_output_file != settings.end())
        {
            auto&& raw_path = it_output_file->second;
            auto content = Strings::join("\n", known_failures);
            content += '\n';
            fs.write_contents_and_dirs(raw_path, content, VCPKG_LINE_INFO);
        }

        binary_cache.wait_for_async_complete_and_join();

        Checks::exit_with_code(VCPKG_LINE_INFO, exit_code);
    }
}
