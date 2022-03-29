#include <vcpkg/base/cache.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/graphs.h>
#include <vcpkg/base/lockguarded.h>
#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/stringliteral.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/view.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/build.h>
#include <vcpkg/ci-baseline.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.ci.h>
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

#include <stdio.h>

using namespace vcpkg;

namespace
{
    using namespace vcpkg::Build;

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

    DECLARE_AND_REGISTER_MESSAGE(
        CiBaselineRegressionHeader,
        (),
        "Printed before a series of CiBaselineRegression and/or CiBaselineUnexpectedPass messages.",
        "REGRESSIONS:");

    DECLARE_AND_REGISTER_MESSAGE(
        CiBaselineRegression,
        (msg::spec, msg::build_result, msg::path),
        "",
        "REGRESSION: {spec} failed with {build_result}. If expected, add {spec}=fail to {path}.");

    DECLARE_AND_REGISTER_MESSAGE(CiBaselineUnexpectedPass,
                                 (msg::spec, msg::path),
                                 "",
                                 "PASSING, REMOVE FROM FAIL LIST: {spec} ({path}).");

    DECLARE_AND_REGISTER_MESSAGE(
        CiBaselineAllowUnexpectedPassingRequiresBaseline,
        (),
        "",
        "--allow-unexpected-passing can only be used if a baseline is provided via --ci-baseline.");
}

namespace vcpkg::Commands::CI
{
    using Build::BuildResult;
    using Dependencies::InstallPlanAction;
    using Dependencies::InstallPlanType;

    struct TripletAndSummary
    {
        Triplet triplet;
        Install::InstallSummary summary;
    };

    static constexpr StringLiteral OPTION_DRY_RUN = "dry-run";
    static constexpr StringLiteral OPTION_EXCLUDE = "exclude";
    static constexpr StringLiteral OPTION_HOST_EXCLUDE = "host-exclude";
    static constexpr StringLiteral OPTION_FAILURE_LOGS = "failure-logs";
    static constexpr StringLiteral OPTION_XUNIT = "x-xunit";
    static constexpr StringLiteral OPTION_CI_BASELINE = "ci-baseline";
    static constexpr StringLiteral OPTION_ALLOW_UNEXPECTED_PASSING = "allow-unexpected-passing";
    static constexpr StringLiteral OPTION_RANDOMIZE = "x-randomize";
    static constexpr StringLiteral OPTION_OUTPUT_HASHES = "output-hashes";
    static constexpr StringLiteral OPTION_PARENT_HASHES = "parent-hashes";
    static constexpr StringLiteral OPTION_SKIPPED_CASCADE_COUNT = "x-skipped-cascade-count";

    static constexpr std::array<CommandSetting, 8> CI_SETTINGS = {
        {{OPTION_EXCLUDE, "Comma separated list of ports to skip"},
         {OPTION_HOST_EXCLUDE, "Comma separated list of ports to skip for the host triplet"},
         {OPTION_XUNIT, "File to output results in XUnit format (internal)"},
         {OPTION_CI_BASELINE, "Path to the ci.baseline.txt file. Used to skip ports and detect regressions."},
         {OPTION_FAILURE_LOGS, "Directory to which failure logs will be copied"},
         {OPTION_OUTPUT_HASHES, "File to output all determined package hashes"},
         {OPTION_PARENT_HASHES,
          "File to read package hashes for a parent CI state, to reduce the set of changed packages"},
         {OPTION_SKIPPED_CASCADE_COUNT,
          "Asserts that the number of --exclude and supports skips exactly equal this number"}}};

    static constexpr std::array<CommandSwitch, 3> CI_SWITCHES = {{
        {OPTION_DRY_RUN, "Print out plan without execution"},
        {OPTION_RANDOMIZE, "Randomize the install order"},
        {OPTION_ALLOW_UNEXPECTED_PASSING,
         "Indicates that 'Passing, remove from fail list' results should not be emitted."},
    }};

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("ci --triplet=x64-windows"),
        0,
        0,
        {CI_SWITCHES, CI_SETTINGS},
        nullptr,
    };

    struct XunitTestResults
    {
    public:
        XunitTestResults() { m_assembly_run_datetime = CTime::get_current_date_time(); }

        void add_test_results(const std::string& spec,
                              const Build::BuildResult& build_result,
                              const ElapsedTime& elapsed_time,
                              const std::string& abi_tag,
                              const std::vector<std::string>& features)
        {
            m_collections.back().tests.push_back({spec, build_result, elapsed_time, abi_tag, features});
        }

        // Starting a new test collection
        void push_collection(const std::string& name) { m_collections.push_back({name}); }

        void collection_time(const vcpkg::ElapsedTime& time) { m_collections.back().time = time; }

        const std::string& build_xml()
        {
            m_xml.clear();
            xml_start_assembly();

            for (const auto& collection : m_collections)
            {
                xml_start_collection(collection);
                for (const auto& test : collection.tests)
                {
                    xml_test(test);
                }
                xml_finish_collection();
            }

            xml_finish_assembly();
            return m_xml;
        }

        void assembly_time(const vcpkg::ElapsedTime& assembly_time) { m_assembly_time = assembly_time; }

    private:
        struct XunitTest
        {
            std::string name;
            vcpkg::Build::BuildResult result;
            vcpkg::ElapsedTime time;
            std::string abi_tag;
            std::vector<std::string> features;
        };

        struct XunitCollection
        {
            std::string name;
            vcpkg::ElapsedTime time;
            std::vector<XunitTest> tests;
        };

        void xml_start_assembly()
        {
            std::string datetime;
            if (m_assembly_run_datetime)
            {
                auto rawDateTime = m_assembly_run_datetime.get()->to_string();
                // The expected format is "yyyy-mm-ddThh:mm:ss.0Z"
                //                         0123456789012345678901
                datetime = Strings::format(
                    R"(run-date="%s" run-time="%s")", rawDateTime.substr(0, 10), rawDateTime.substr(11, 8));
            }

            std::string time = Strings::format(R"(time="%lld")", m_assembly_time.as<std::chrono::seconds>().count());

            m_xml += Strings::format(R"(<assemblies>)"
                                     "\n"
                                     R"(  <assembly name="vcpkg" %s %s>)"
                                     "\n",
                                     datetime,
                                     time);
        }
        void xml_finish_assembly()
        {
            m_xml += "  </assembly>\n"
                     "</assemblies>\n";
        }

        void xml_start_collection(const XunitCollection& collection)
        {
            m_xml += Strings::format(R"(    <collection name="%s" time="%lld">)"
                                     "\n",
                                     collection.name,
                                     collection.time.as<std::chrono::seconds>().count());
        }
        void xml_finish_collection() { m_xml += "    </collection>\n"; }

        void xml_test(const XunitTest& test)
        {
            std::string message_block;
            const char* result_string = "";
            switch (test.result)
            {
                case BuildResult::POST_BUILD_CHECKS_FAILED:
                case BuildResult::FILE_CONFLICTS:
                case BuildResult::BUILD_FAILED:
                    result_string = "Fail";
                    message_block = Strings::format("<failure><message><![CDATA[%s]]></message></failure>",
                                                    to_string_locale_invariant(test.result));
                    break;
                case BuildResult::EXCLUDED:
                case BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES:
                    result_string = "Skip";
                    message_block =
                        Strings::format("<reason><![CDATA[%s]]></reason>", to_string_locale_invariant(test.result));
                    break;
                case BuildResult::SUCCEEDED: result_string = "Pass"; break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }

            std::string traits_block;
            if (!test.abi_tag.empty())
            {
                traits_block += Strings::format(R"(<trait name="abi_tag" value="%s" />)", test.abi_tag);
            }

            if (!test.features.empty())
            {
                std::string feature_list;
                for (const auto& feature : test.features)
                {
                    if (!feature_list.empty())
                    {
                        feature_list += ", ";
                    }
                    feature_list += feature;
                }

                traits_block += Strings::format(R"(<trait name="features" value="%s" />)", feature_list);
            }

            if (!traits_block.empty())
            {
                traits_block = "<traits>" + traits_block + "</traits>";
            }

            m_xml += Strings::format(R"(      <test name="%s" method="%s" time="%lld" result="%s">%s%s</test>)"
                                     "\n",
                                     test.name,
                                     test.name,
                                     test.time.as<std::chrono::seconds>().count(),
                                     result_string,
                                     traits_block,
                                     message_block);
        }

        Optional<vcpkg::CTime> m_assembly_run_datetime;
        vcpkg::ElapsedTime m_assembly_time;
        std::vector<XunitCollection> m_collections;

        std::string m_xml;
    };

    struct UnknownCIPortsResults
    {
        std::map<PackageSpec, Build::BuildResult> known;
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

    static Dependencies::ActionPlan compute_full_plan(const VcpkgPaths& paths,
                                                      const PortFileProvider::PortFileProvider& provider,
                                                      const CMakeVars::CMakeVarProvider& var_provider,
                                                      const std::vector<FullPackageSpec>& specs,
                                                      const Dependencies::CreateInstallPlanOptions& serialize_options)
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
        auto action_plan =
            Dependencies::create_feature_install_plan(provider, var_provider, specs, {}, serialize_options);

        var_provider.load_tag_vars(action_plan, provider, serialize_options.host_triplet);

        Checks::check_exit(VCPKG_LINE_INFO, action_plan.already_installed.empty());
        Checks::check_exit(VCPKG_LINE_INFO, action_plan.remove_actions.empty());

        Build::compute_all_abis(paths, action_plan, var_provider, {});
        return action_plan;
    }

    static std::unique_ptr<UnknownCIPortsResults> compute_action_statuses(
        ExclusionPredicate is_excluded,
        const CMakeVars::CMakeVarProvider& var_provider,
        const std::vector<CacheAvailability>& precheck_results,
        const Dependencies::ActionPlan& action_plan)
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
                ret->action_state_string.push_back("skip");
                ret->known.emplace(p->spec, BuildResult::EXCLUDED);
                will_fail.emplace(p->spec);
            }
            else if (!supported_for_triplet(var_provider, p))
            {
                // This treats unsupported ports as if they are excluded
                // which means the ports dependent on it will be cascaded due to missing dependencies
                // Should this be changed so instead it is a failure to depend on a unsupported port?
                ret->action_state_string.push_back("n/a");
                ret->known.emplace(p->spec, BuildResult::EXCLUDED);
                will_fail.emplace(p->spec);
            }
            else if (Util::any_of(p->package_dependencies,
                                  [&](const PackageSpec& spec) { return Util::Sets::contains(will_fail, spec); }))
            {
                ret->action_state_string.push_back("cascade");
                ret->cascade_count++;
                ret->known.emplace(p->spec, BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES);
                will_fail.emplace(p->spec);
            }
            else if (precheck_results[action_idx] == CacheAvailability::available)
            {
                ret->action_state_string.push_back("pass");
                ret->known.emplace(p->spec, BuildResult::SUCCEEDED);
            }
            else
            {
                ret->action_state_string.push_back("*");
            }
        }
        return ret;
    }

    // This algorithm reduces an action plan to only unknown actions and their dependencies
    static void reduce_action_plan(Dependencies::ActionPlan& action_plan,
                                   const std::map<PackageSpec, Build::BuildResult>& known,
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
                    it->build_options = vcpkg::Build::backcompat_prohibiting_package_options;
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

    static Optional<int> parse_skipped_cascade_count(const std::map<std::string, std::string, std::less<>>& settings)
    {
        auto opt = settings.find(OPTION_SKIPPED_CASCADE_COUNT);
        if (opt == settings.end())
        {
            return nullopt;
        }

        auto result = Strings::strto<int>(opt->second);
        Checks::check_exit(VCPKG_LINE_INFO, result.has_value(), "%s must be an integer", OPTION_SKIPPED_CASCADE_COUNT);
        Checks::check_exit(VCPKG_LINE_INFO,
                           result.value_or_exit(VCPKG_LINE_INFO) >= 0,
                           "%s must be non-negative",
                           OPTION_SKIPPED_CASCADE_COUNT);
        return result;
    }

    static void print_baseline_regressions(const TripletAndSummary& result,
                                           const SortedVector<PackageSpec>& expected_failures,
                                           const std::string& ci_baseline_file_name,
                                           bool allow_unexpected_passing)
    {
        LocalizedString output;
        for (auto&& port_result : result.summary.results)
        {
            switch (port_result.build_result.code)
            {
                case Build::BuildResult::BUILD_FAILED:
                case Build::BuildResult::POST_BUILD_CHECKS_FAILED:
                case Build::BuildResult::FILE_CONFLICTS:
                    if (!expected_failures.contains(port_result.spec))
                    {
                        output.append(msg::format(
                            msgCiBaselineRegression,
                            msg::spec = port_result.spec.to_string(),
                            msg::build_result =
                                Build::to_string_locale_invariant(port_result.build_result.code).to_string(),
                            msg::path = ci_baseline_file_name));
                        output.appendnl();
                    }
                    break;
                case Build::BuildResult::SUCCEEDED:
                    if (!allow_unexpected_passing && expected_failures.contains(port_result.spec))
                    {
                        output.append(msg::format(msgCiBaselineUnexpectedPass,
                                                  msg::spec = port_result.spec.to_string(),
                                                  msg::path = ci_baseline_file_name));
                        output.appendnl();
                    }
                    break;
                default: break;
            }
        }

        auto output_data = output.extract_data();
        if (output_data.empty())
        {
            return;
        }

        LocalizedString header = msg::format(msgCiBaselineRegressionHeader);
        header.appendnl();
        output_data.insert(0, header.extract_data());
        fwrite(output_data.data(), 1, output_data.size(), stderr);
    }

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet target_triplet,
                          Triplet host_triplet)
    {
        vcpkg::print2(Color::warning,
                      "'vcpkg ci' is an internal command which will change incompatibly or be removed at any time.\n");

        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);
        const auto& settings = options.settings;

        BinaryCache binary_cache{args, paths};

        ExclusionsMap exclusions_map;
        parse_exclusions(settings, OPTION_EXCLUDE, target_triplet, exclusions_map);
        parse_exclusions(settings, OPTION_HOST_EXCLUDE, host_triplet, exclusions_map);
        auto baseline_iter = settings.find(OPTION_CI_BASELINE);
        const bool allow_unexpected_passing = Util::Sets::contains(options.switches, OPTION_ALLOW_UNEXPECTED_PASSING);
        SortedVector<PackageSpec> expected_failures;
        if (baseline_iter == settings.end())
        {
            if (allow_unexpected_passing)
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgCiBaselineAllowUnexpectedPassingRequiresBaseline);
            }
        }
        else
        {
            const auto& ci_baseline_file_name = baseline_iter->second;
            const auto ci_baseline_file_contents =
                paths.get_filesystem().read_contents(ci_baseline_file_name, VCPKG_LINE_INFO);
            ParseMessages ci_parse_messages;
            const auto lines = parse_ci_baseline(ci_baseline_file_contents, ci_baseline_file_name, ci_parse_messages);
            ci_parse_messages.exit_if_errors_or_warnings(ci_baseline_file_name);
            expected_failures = parse_and_apply_ci_baseline(lines, exclusions_map);
        }

        auto skipped_cascade_count = parse_skipped_cascade_count(settings);

        const auto is_dry_run = Util::Sets::contains(options.switches, OPTION_DRY_RUN);

        auto& filesystem = paths.get_filesystem();
        Optional<CiBuildLogsRecorder> build_logs_recorder_storage;
        {
            auto it_failure_logs = settings.find(OPTION_FAILURE_LOGS);
            if (it_failure_logs != settings.end())
            {
                vcpkg::printf("Creating failure logs output directory %s\n", it_failure_logs->second);
                Path raw_path = it_failure_logs->second;
                filesystem.create_directories(raw_path, VCPKG_LINE_INFO);
                build_logs_recorder_storage = filesystem.almost_canonical(raw_path, VCPKG_LINE_INFO);
            }
        }

        const IBuildLogsRecorder& build_logs_recorder =
            build_logs_recorder_storage ? *(build_logs_recorder_storage.get()) : null_build_logs_recorder();

        PortFileProvider::PathsPortFileProvider provider(paths, args.overlay_ports);
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        std::vector<std::map<PackageSpec, BuildResult>> all_known_results;

        XunitTestResults xunitTestResults;

        std::vector<TripletAndSummary> results;
        auto timer = ElapsedTimer::create_started();

        xunitTestResults.push_collection(target_triplet.canonical_name());

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

        Dependencies::CreateInstallPlanOptions serialize_options(host_triplet,
                                                                 Dependencies::UnsupportedPortAction::Warn);

        struct RandomizerInstance : Graphs::Randomizer
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
                filesystem.write_contents(output_hash_json, Json::stringify(arr, Json::JsonStyle{}), VCPKG_LINE_INFO);
            }
        }

        std::vector<std::string> parent_hashes;

        auto it_parent_hashes = settings.find(OPTION_PARENT_HASHES);
        if (it_parent_hashes != settings.end())
        {
            const Path parent_hashes_path = paths.original_cwd / it_parent_hashes->second;
            auto parsed_json = Json::parse_file(VCPKG_LINE_INFO, filesystem, parent_hashes_path);
            parent_hashes = Util::fmap(parsed_json.first.array(), [](const auto& json_object) {
                auto abi = json_object.object().get("abi");
                Checks::check_exit(VCPKG_LINE_INFO, abi);
#ifdef _MSC_VER
                _Analysis_assume_(abi);
#endif
                return abi->string().to_string();
            });
        }

        reduce_action_plan(action_plan, split_specs->known, parent_hashes);

        vcpkg::printf("Time to determine pass/fail: %s\n", timer.elapsed());

        if (auto skipped_cascade_count_ptr = skipped_cascade_count.get())
        {
            Checks::check_exit(VCPKG_LINE_INFO,
                               *skipped_cascade_count_ptr == split_specs->cascade_count,
                               "Expected %d cascaded failures, but there were %d cascaded failures.",
                               *skipped_cascade_count_ptr,
                               split_specs->cascade_count);
        }

        if (is_dry_run)
        {
            Dependencies::print_plan(action_plan, true, paths.builtin_ports_directory());
        }
        else
        {
            StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());

            auto collection_timer = ElapsedTimer::create_started();
            auto summary = Install::perform(args,
                                            action_plan,
                                            Install::KeepGoing::YES,
                                            paths,
                                            status_db,
                                            binary_cache,
                                            build_logs_recorder,
                                            var_provider);
            auto collection_time_elapsed = collection_timer.elapsed();

            // Adding results for ports that were built or pulled from an archive
            for (auto&& result : summary.results)
            {
                auto& port_features = split_specs->features.at(result.spec);
                split_specs->known.erase(result.spec);
                xunitTestResults.add_test_results(result.spec.to_string(),
                                                  result.build_result.code,
                                                  result.timing,
                                                  split_specs->abi_map.at(result.spec),
                                                  port_features);
            }

            // Adding results for ports that were not built because they have known states
            for (auto&& port : split_specs->known)
            {
                auto& port_features = split_specs->features.at(port.first);
                xunitTestResults.add_test_results(port.first.to_string(),
                                                  port.second,
                                                  ElapsedTime{},
                                                  split_specs->abi_map.at(port.first),
                                                  port_features);
            }

            all_known_results.emplace_back(std::move(split_specs->known));

            results.push_back({target_triplet, std::move(summary)});

            xunitTestResults.collection_time(collection_time_elapsed);
        }

        xunitTestResults.assembly_time(timer.elapsed());

        for (auto&& result : results)
        {
            print2("\nTriplet: ", result.triplet, "\n");
            print2("Total elapsed time: ", GlobalState::timer.to_string(), "\n");
            result.summary.print();

            if (baseline_iter != settings.end())
            {
                print_baseline_regressions(result, expected_failures, baseline_iter->second, allow_unexpected_passing);
            }
        }

        auto it_xunit = settings.find(OPTION_XUNIT);
        if (it_xunit != settings.end())
        {
            filesystem.write_contents(it_xunit->second, xunitTestResults.build_xml(), VCPKG_LINE_INFO);
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
