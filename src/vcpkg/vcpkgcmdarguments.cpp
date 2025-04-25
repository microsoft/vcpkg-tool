#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <algorithm>
#include <string>
#include <utility>

namespace
{
    using namespace vcpkg;

    struct CIRecord
    {
        StringLiteral env_var;
        StringLiteral name;
        CIKind type;
    };

    constexpr CIRecord KNOWN_CI_VARIABLES[]{
        // Opt-out from CI detection
        {EnvironmentVariableVcpkgNoCi, "VCPKG_NO_CI", CIKind::None},

        // Azure Pipelines
        // https://docs.microsoft.com/en-us/azure/devops/pipelines/build/variables#system-variables
        {EnvironmentVariableTfBuild, "Azure_Pipelines", CIKind::AzurePipelines},

        // AppVeyor
        // https://www.appveyor.com/docs/environment-variables/
        {EnvironmentVariableAppveyor, "AppVeyor", CIKind::AppVeyor},

        // AWS Code Build
        // https://docs.aws.amazon.com/codebuild/latest/userguide/build-env-ref-env-vars.html
        {EnvironmentVariableCodebuildBuildId, "AWS_CodeBuild", CIKind::AwsCodeBuild},

        // CircleCI
        // https://circleci.com/docs/env-vars#built-in-environment-variables
        {EnvironmentVariableCircleCI, "Circle_CI", CIKind::CircleCI},

        // GitHub Actions
        // https://docs.github.com/en/actions/learn-github-actions/
        {EnvironmentVariableGitHubActions, "GitHub_Actions", CIKind::GithubActions},

        // GitLab
        // https://docs.gitlab.com/ee/ci/variables/predefined_variables.html
        {EnvironmentVariableGitLabCI, "GitLab_CI", CIKind::GitLabCI},

        // Heroku
        // https://devcenter.heroku.com/articles/heroku-ci#immutable-environment-variables
        {EnvironmentVariableHerokuTestRunId, "Heroku_CI", CIKind::HerokuCI},

        // Jenkins
        // https://wiki.jenkins.io/display/JENKINS/Building+a+software+project#Buildingasoftwareproject-belowJenkinsSetEnvironmentVariables
        {EnvironmentVariableJenkinsHome, "Jenkins_CI", CIKind::JenkinsCI},
        {EnvironmentVariableJenkinsUrl, "Jenkins_CI", CIKind::JenkinsCI},

        // TeamCity
        // https://www.jetbrains.com/help/teamcity/predefined-build-parameters.html#Predefined+Server+Build+Parameters
        {EnvironmentVariableTeamcityVersion, "TeamCity_CI", CIKind::TeamCityCI},

        // Travis CI
        // https://docs.travis-ci.com/user/environment-variables/#default-environment-variables
        {EnvironmentVariableTravis, "Travis_CI", CIKind::TravisCI},

        // Generic CI environment variables
        {EnvironmentVariableCI, "Generic", CIKind::Generic},
        {EnvironmentVariableBuildId, "Generic", CIKind::Generic},
        {EnvironmentVariableBuildNumber, "Generic", CIKind::Generic},
    };

    constexpr StringLiteral KNOWN_CI_REPOSITORY_IDENTIFIERS[] = {
        // Azure Pipelines
        // https://learn.microsoft.com/azure/devops/pipelines/build/variables?view=azure-devops&tabs=yaml#build-variables-devops-services
        EnvironmentVariableBuildRepositoryId,
        // GitLab CI
        // https://docs.gitlab.com/ee/ci/variables/predefined_variables.html
        EnvironmentVariableCIProjectId,
        // GitHub Actions
        // https://docs.github.com/actions/learn-github-actions/variables#default-environment-variables
        EnvironmentVariableGitHubRepositoryID,
    };

    void maybe_parse_cmd_arguments(CmdParser& cmd_parser,
                                   ParsedArguments& output,
                                   const CommandMetadata& command_metadata)
    {
        for (const auto& switch_ : command_metadata.options.switches)
        {
            bool parse_result;
            ZStringView name = switch_.name;
            StabilityTag tag = StabilityTag::Standard;
            if (name.starts_with("x-"))
            {
                name = name.substr(2);
                tag = StabilityTag::Experimental;
            }
            else if (name.starts_with("z-"))
            {
                name = name.substr(2);
                tag = StabilityTag::ImplementationDetail;
            }

            if (switch_.helpmsg)
            {
                if (cmd_parser.parse_switch(name, tag, parse_result, switch_.helpmsg.to_string()) && parse_result)
                {
                    output.switches.emplace(switch_.name);
                }
            }
            else
            {
                if (cmd_parser.parse_switch(name, tag, parse_result) && parse_result)
                {
                    output.switches.emplace(switch_.name);
                }
            }
        }

        {
            std::string maybe_parse_result;
            for (const auto& option : command_metadata.options.settings)
            {
                ZStringView name = option.name;
                StabilityTag tag = StabilityTag::Standard;
                if (name.starts_with("x-"))
                {
                    name = name.substr(2);
                    tag = StabilityTag::Experimental;
                }
                else if (name.starts_with("z-"))
                {
                    name = name.substr(2);
                    tag = StabilityTag::ImplementationDetail;
                }

                if (option.helpmsg)
                {
                    if (cmd_parser.parse_option(name, tag, maybe_parse_result, option.helpmsg.to_string()))
                    {
                        output.settings.emplace(option.name, std::move(maybe_parse_result));
                    }
                }
                else
                {
                    if (cmd_parser.parse_option(name, tag, maybe_parse_result))
                    {
                        output.settings.emplace(option.name, std::move(maybe_parse_result));
                    }
                }
            }
        }

        for (const auto& option : command_metadata.options.multisettings)
        {
            ZStringView name = option.name;
            StabilityTag tag = StabilityTag::Standard;
            if (name.starts_with("x-"))
            {
                name = name.substr(2);
                tag = StabilityTag::Experimental;
            }
            else if (name.starts_with("z-"))
            {
                name = name.substr(2);
                tag = StabilityTag::ImplementationDetail;
            }

            std::vector<std::string> maybe_parse_result;
            if (option.helpmsg)
            {
                if (cmd_parser.parse_multi_option(name, tag, maybe_parse_result, option.helpmsg.to_string()))
                {
                    output.multisettings.emplace(option.name, std::move(maybe_parse_result));
                }
            }
            else
            {
                if (cmd_parser.parse_multi_option(name, tag, maybe_parse_result))
                {
                    output.multisettings.emplace(option.name, std::move(maybe_parse_result));
                }
            }
        }
    }
}

namespace vcpkg
{
    const std::string* ParsedArguments::read_setting(StringLiteral setting) const noexcept
    {
        auto loc = settings.find(setting);
        if (loc == settings.end())
        {
            return nullptr;
        }

        return &loc->second;
    }

    LocalizedString MetadataMessage::to_string() const
    {
        switch (kind)
        {
            case MetadataMessageKind::Message: return msg::format(*message);
            case MetadataMessageKind::Literal: return LocalizedString::from_raw(literal);
            case MetadataMessageKind::Callback: return callback();
            case MetadataMessageKind::Unused:
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    void MetadataMessage::to_string(LocalizedString& target) const
    {
        switch (kind)
        {
            case MetadataMessageKind::Message: msg::format_to(target, *message); return;
            case MetadataMessageKind::Literal: target.append_raw(literal); return;
            case MetadataMessageKind::Callback: target.append(callback()); return;
            case MetadataMessageKind::Unused:
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    MetadataMessage::operator bool() const noexcept { return kind != MetadataMessageKind::Unused; }

    LocalizedString LearnWebsiteLinkLiteral::to_string() const { return LocalizedString::from_raw(literal); }

    void LearnWebsiteLinkLiteral::to_string(LocalizedString& target) const { target.append_raw(literal); }

    LearnWebsiteLinkLiteral::operator bool() const noexcept { return literal != nullptr; }

    static void set_from_feature_flag(const std::vector<std::string>& flags, StringView flag, Optional<bool>& place)
    {
        if (!place.has_value())
        {
            const auto not_flag = [flag](const std::string& el) {
                return !el.empty() && el[0] == '-' && flag == StringView{el.data() + 1, el.data() + el.size()};
            };

            if (std::find(flags.begin(), flags.end(), flag) != flags.end())
            {
                place = true;
            }
            if (std::find_if(flags.begin(), flags.end(), not_flag) != flags.end())
            {
                if (place.has_value())
                {
                    Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgTwoFeatureFlagsSpecified, msg::value = flag);
                }

                place = false;
            }
        }
    }

    static void parse_feature_flags(const std::vector<std::string>& flags, VcpkgCmdArguments& args)
    {
        // NOTE: when these features become default, switch the value_or(false) to value_or(true)
        struct FeatureFlag
        {
            StringLiteral flag_name;
            Optional<bool>& local_option;
        };

        // Parsed for command line backcompat, but cannot be disabled
        Optional<bool> manifest_mode;
        const FeatureFlag flag_descriptions[] = {
            {FeatureFlagBinarycaching, args.binary_caching},
            {FeatureFlagManifests, manifest_mode},
            {FeatureFlagCompilertracking, args.compiler_tracking},
            {FeatureFlagRegistries, args.registries_feature},
            {FeatureFlagVersions, args.versions_feature},
            {FeatureFlagDependencygraph, args.dependency_graph_feature},
        };

        for (const auto& desc : flag_descriptions)
        {
            set_from_feature_flag(flags, desc.flag_name, desc.local_option);
        }
    }

    PortApplicableSetting::PortApplicableSetting(StringView setting)
    {
        auto split = Strings::split(setting, ';');
        if (!split.empty())
        {
            value = std::move(split[0]);
            split.erase(split.begin());
            Util::sort(split);
            affected_ports = std::move(split);
        }
    }

    bool PortApplicableSetting::is_port_affected(StringView port_name) const noexcept
    {
        return affected_ports.empty() || std::binary_search(affected_ports.begin(), affected_ports.end(), port_name);
    }

    VcpkgCmdArguments VcpkgCmdArguments::create_from_command_line(const ILineReader& fs,
                                                                  const int argc,
                                                                  const CommandLineCharType* const* const argv)
    {
        std::vector<std::string> v = convert_argc_argv_to_arguments(argc, argv);
        replace_response_file_parameters(v, fs).value_or_exit(VCPKG_LINE_INFO);
        return VcpkgCmdArguments::create_from_arg_sequence(v.data(), v.data() + v.size());
    }

    VcpkgCmdArguments VcpkgCmdArguments::create_from_arg_sequence(const std::string* arg_first,
                                                                  const std::string* arg_last)
    {
        VcpkgCmdArguments args{CmdParser{View<std::string>{arg_first, arg_last}}};
        args.parser.parse_switch(SwitchDebug, StabilityTag::Standard, args.debug);
        args.parser.parse_switch(SwitchDebugEnv, StabilityTag::Standard, args.debug_env);
        args.parser.parse_switch(SwitchDisableMetrics, StabilityTag::Standard, args.disable_metrics);
        args.parser.parse_switch(SwitchSendmetrics, StabilityTag::Standard, args.send_metrics);
        args.parser.parse_switch(SwitchPrintmetrics, StabilityTag::Standard, args.print_metrics);
        args.parser.parse_switch(FeatureFlagFeaturepackages, StabilityTag::Standard, args.feature_packages);
        args.parser.parse_switch(SwitchBinarycaching, StabilityTag::Standard, args.binary_caching);
        args.parser.parse_switch(SwitchWaitForLock, StabilityTag::Experimental, args.wait_for_lock);
        args.parser.parse_switch(SwitchIgnoreLockFailures, StabilityTag::Experimental, args.ignore_lock_failures);
        args.parser.parse_switch(
            SwitchAbiToolsUseExactVersions, StabilityTag::Experimental, args.exact_abi_tools_versions);

        args.parser.parse_option(
            SwitchVcpkgRoot,
            StabilityTag::Standard,
            args.vcpkg_root_dir_arg,
            msg::format(msgVcpkgRootsDir, msg::env_var = format_environment_variable(EnvironmentVariableVcpkgRoot)));
        args.parser.parse_option(
            SwitchTriplet,
            StabilityTag::Standard,
            args.triplet,
            msg::format(msgSpecifyTargetArch,
                        msg::env_var = format_environment_variable(EnvironmentVariableVcpkgDefaultTriplet)));
        args.parser.parse_option(
            SwitchHostTriplet,
            StabilityTag::Standard,
            args.host_triplet,
            msg::format(msgSpecifyHostArch,
                        msg::env_var = format_environment_variable(EnvironmentVariableVcpkgDefaultHostTriplet)));
        args.parser.parse_switch(
            SwitchClassic, StabilityTag::Standard, args.force_classic_mode, msg::format(msgForceClassicMode));
        args.parser.parse_option(SwitchManifestRoot, StabilityTag::Experimental, args.manifest_root_dir);
        args.parser.parse_option(SwitchBuildtreesRoot,
                                 StabilityTag::Experimental,
                                 args.buildtrees_root_dir,
                                 msg::format(msgBuildTreesRootDir));
        args.parser.parse_option(
            SwitchDownloadsRoot,
            StabilityTag::Standard,
            args.downloads_root_dir,
            msg::format(msgDownloadRootsDir,
                        msg::env_var = format_environment_variable(EnvironmentVariableVcpkgDownloads)));
        args.parser.parse_option(
            SwitchInstallRoot, StabilityTag::Experimental, args.install_root_dir, msg::format(msgInstallRootDir));
        args.parser.parse_option(
            SwitchPackagesRoot, StabilityTag::Experimental, args.packages_root_dir, msg::format(msgPackageRootDir));
        args.parser.parse_option(SwitchScriptsRoot, StabilityTag::Experimental, args.scripts_root_dir);
        args.parser.parse_option(SwitchBuiltinPortsRoot, StabilityTag::Experimental, args.builtin_ports_root_dir);
        args.parser.parse_option(
            SwitchBuiltinRegistryVersionsDir, StabilityTag::Experimental, args.builtin_registry_versions_dir);
        args.parser.parse_option(SwitchRegistriesCache, StabilityTag::Experimental, args.registries_cache_dir);
        args.parser.parse_option(SwitchToolDataFile, StabilityTag::ImplementationDetail, args.tools_data_file);
        args.parser.parse_option(SwitchAssetSources,
                                 StabilityTag::Experimental,
                                 args.asset_sources_template_arg,
                                 msg::format(msgAssetSourcesArg));
        {
            std::string raw_cmake_debug;
            if (args.parser.parse_option(SwitchCMakeDebug, StabilityTag::Experimental, raw_cmake_debug))
            {
                args.cmake_debug.emplace(raw_cmake_debug);
            }

            if (args.parser.parse_option(SwitchCMakeConfigureDebug, StabilityTag::Experimental, raw_cmake_debug))
            {
                args.cmake_configure_debug.emplace(raw_cmake_debug);
            }
        }

        args.parser.parse_multi_option(
            SwitchOverlayPorts,
            StabilityTag::Standard,
            args.cli_overlay_ports,
            msg::format(msgOverlayPortsHelp,
                        msg::env_var = format_environment_variable(EnvironmentVariableVcpkgOverlayPorts)));
        args.parser.parse_multi_option(
            SwitchOverlayTriplets,
            StabilityTag::Standard,
            args.cli_overlay_triplets,
            msg::format(msgOverlayTripletDirectoriesHelp,
                        msg::env_var = format_environment_variable(EnvironmentVariableOverlayTriplets)));
        args.parser.parse_multi_option(
            SwitchBinarysource, StabilityTag::Standard, args.cli_binary_sources, msg::format(msgBinarySourcesArg));
        args.parser.parse_multi_option(SwitchCMakeArgs, StabilityTag::Standard, args.cmake_args);

        std::vector<std::string> feature_flags;
        args.parser.parse_multi_option(SwitchFeatureFlags, StabilityTag::Standard, feature_flags);
        delistify_conjoined_multivalue(feature_flags);
        parse_feature_flags(feature_flags, args);

        // --debug-env implies --debug
        if (const auto p = args.debug_env.get()) args.debug = *p;

        auto maybe_command = args.parser.extract_first_command_like_arg_lowercase();
        if (auto command = maybe_command.get())
        {
            args.command = *command;
        }

        args.forwardable_arguments = args.parser.get_remaining_args();
        auto&& initial_parser_errors = args.parser.get_errors();
        if (!initial_parser_errors.empty())
        {
            msg::write_unlocalized_text_to_stderr(Color::error, Strings::join("\n", initial_parser_errors) + "\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        return args;
    }

    ParsedArguments VcpkgCmdArguments::parse_arguments(const CommandMetadata& command_metadata) const
    {
        ParsedArguments output;
        auto cmd_parser = this->parser;
        maybe_parse_cmd_arguments(cmd_parser, output, command_metadata);
        if (command_metadata.maximum_arity == 0)
        {
            cmd_parser.enforce_no_remaining_args(command);
        }
        else if (command_metadata.minimum_arity == 0 && command_metadata.maximum_arity == 1)
        {
            auto maybe_arg = cmd_parser.consume_only_remaining_arg_optional(command);
            if (auto arg = maybe_arg.get())
            {
                output.command_arguments.push_back(std::move(*arg));
            }
        }
        else if (command_metadata.minimum_arity == 1 && command_metadata.maximum_arity == 1)
        {
            output.command_arguments.push_back(cmd_parser.consume_only_remaining_arg(command));
        }
        else if (command_metadata.minimum_arity == command_metadata.maximum_arity)
        {
            output.command_arguments = cmd_parser.consume_remaining_args(command, command_metadata.minimum_arity);
        }
        else
        {
            output.command_arguments = cmd_parser.consume_remaining_args(
                command, command_metadata.minimum_arity, command_metadata.maximum_arity);
        }

        cmd_parser.exit_with_errors(command_metadata.get_example_text());
        return output;
    }

    const std::vector<std::string>& VcpkgCmdArguments::get_forwardable_arguments() const noexcept
    {
        return forwardable_arguments;
    }

    VcpkgCmdArguments::VcpkgCmdArguments(CmdParser&& parser_) : parser(std::move(parser_)) { }

    LocalizedString CommandMetadata::get_example_text() const
    {
        LocalizedString result;
        if (examples[0])
        {
            examples[0].to_string(result);
            for (std::size_t idx = 1; idx < example_max_size; ++idx)
            {
                if (examples[idx])
                {
                    result.append_raw('\n');
                    examples[idx].to_string(result);
                }
            }
        }

        return result;
    }

    LocalizedString usage_for_command(const CommandMetadata& command_metadata)
    {
        auto with_common_options = VcpkgCmdArguments::create_from_arg_sequence(nullptr, nullptr);
        ParsedArguments throwaway;
        maybe_parse_cmd_arguments(with_common_options.parser, throwaway, command_metadata);
        LocalizedString result;
        if (command_metadata.synopsis)
        {
            result.append(msgSynopsisHeader);
            result.append_raw(' ');
            command_metadata.synopsis.to_string(result);
            result.append_raw('\n');
        }

        std::vector<LocalizedString> examples;
        for (auto&& maybe_example : command_metadata.examples)
        {
            if (maybe_example)
            {
                examples.push_back(maybe_example.to_string());
            }
        }

        if (!examples.empty())
        {
            result.append(msgExamplesHeader);
            result.append_floating_list(1, examples);
            result.append_raw('\n');
        }

        if (command_metadata.website_link)
        {
            result.append(msgSeeURL, msg::url = command_metadata.website_link.to_string()).append_raw('\n');
        }

        with_common_options.parser.append_options_table(result);
        result.append_raw('\n');
        return result;
    }

    static void from_env(const std::function<Optional<std::string>(ZStringView)>& f,
                         ZStringView var,
                         Optional<std::string>& dst)
    {
        if (dst) return;

        dst = f(var);
    }

    void VcpkgCmdArguments::imbue_from_environment() { imbue_from_environment_impl(&vcpkg::get_environment_variable); }
    void VcpkgCmdArguments::imbue_from_fake_environment(const std::map<StringLiteral, std::string, std::less<>>& env)
    {
        imbue_from_environment_impl([&env](ZStringView var) -> Optional<std::string> {
            auto it = env.find(var);
            if (it == env.end())
            {
                return nullopt;
            }
            else
            {
                return it->second;
            }
        });
    }
    void VcpkgCmdArguments::imbue_from_environment_impl(std::function<Optional<std::string>(ZStringView)> get_env)
    {
        if (!disable_metrics)
        {
            const auto vcpkg_disable_metrics_env = get_env(EnvironmentVariableVcpkgDisableMetrics);
            if (vcpkg_disable_metrics_env.has_value())
            {
                disable_metrics = true;
            }
        }

        from_env(get_env, EnvironmentVariableVcpkgDefaultTriplet, triplet);
        from_env(get_env, EnvironmentVariableVcpkgDefaultHostTriplet, host_triplet);
        vcpkg_root_dir_env = get_env(EnvironmentVariableVcpkgRoot);
        from_env(get_env, EnvironmentVariableVcpkgDownloads, downloads_root_dir);
        from_env(get_env, EnvironmentVariableXVcpkgAssetSources, asset_sources_template_env);
        from_env(get_env, EnvironmentVariableXVcpkgRegistriesCache, registries_cache_dir);
        from_env(get_env, EnvironmentVariableVcpkgVisualStudioPath, default_visual_studio_path);
        from_env(get_env, EnvironmentVariableVcpkgBinarySources, env_binary_sources);
        from_env(get_env, EnvironmentVariableXVcpkgNuGetIDPrefix, nuget_id_prefix);
        use_nuget_cache = get_env(EnvironmentVariableVcpkgUseNuGetCache).map([](const std::string& s) {
            return Strings::case_insensitive_ascii_equals(s, "true") || s == "1";
        });
        from_env(get_env, EnvironmentVariableVcpkgNuGetRepository, vcpkg_nuget_repository);
        from_env(get_env, EnvironmentVariableGitHubRepository, github_repository);
        from_env(get_env, EnvironmentVariableGitHubServerUrl, github_server_url);
        from_env(get_env, EnvironmentVariableGitHubRef, github_ref);
        from_env(get_env, EnvironmentVariableGitHubSha, github_sha);
        from_env(get_env, EnvironmentVariableGitHubJob, github_job);
        from_env(get_env, EnvironmentVariableGitHubRepositoryOwnerId, ci_repository_owner_id);
        from_env(get_env, EnvironmentVariableGitHubRunId, github_run_id);
        from_env(get_env, EnvironmentVariableGitHubToken, github_token);
        from_env(get_env, EnvironmentVariableGitHubWorkflow, github_workflow);

        // detect whether we are running in a CI environment
        for (auto&& ci_env_var : KNOWN_CI_VARIABLES)
        {
            if (get_env(ci_env_var.env_var).has_value())
            {
                m_detected_ci_environment_name = ci_env_var.name;
                m_detected_ci_environment_type = ci_env_var.type;
                break;
            }
        }

        // detect unique repository identifier in CI environments
        for (auto&& ci_id_var : KNOWN_CI_REPOSITORY_IDENTIFIERS)
        {
            auto maybe_ci_id = get_env(ci_id_var);
            if (auto ci_id = maybe_ci_id.get())
            {
                ci_repository_id = std::move(*ci_id);
                break;
            }
        }

        {
            const auto vcpkg_disable_lock = get_env(EnvironmentVariableXVcpkgIgnoreLockFailures);
            if (vcpkg_disable_lock.has_value() && !ignore_lock_failures.has_value())
            {
                ignore_lock_failures = true;
            }
        }

        {
            const auto vcpkg_overlay_ports_env = get_env(EnvironmentVariableVcpkgOverlayPorts);
            if (const auto unpacked = vcpkg_overlay_ports_env.get())
            {
                env_overlay_ports = Strings::split_paths(*unpacked);
            }
        }
        {
            const auto vcpkg_overlay_triplets_env = get_env(EnvironmentVariableOverlayTriplets);
            if (const auto unpacked = vcpkg_overlay_triplets_env.get())
            {
                env_overlay_triplets = Strings::split_paths(*unpacked);
            }
        }
        {
            const auto vcpkg_feature_flags_env = get_env(EnvironmentVariableVcpkgFeatureFlags);
            if (const auto v = vcpkg_feature_flags_env.get())
            {
                auto flags = Strings::split(*v, ',');
                parse_feature_flags(flags, *this);
            }
        }
    }

    void VcpkgCmdArguments::imbue_or_apply_process_recursion(VcpkgCmdArguments& args)
    {
        static bool s_reentrancy_guard = false;
        Checks::check_exit(
            VCPKG_LINE_INFO,
            !s_reentrancy_guard,
            "VcpkgCmdArguments::imbue_or_apply_process_recursion() modifies global state and thus may only be "
            "called once per process.");
        s_reentrancy_guard = true;

        auto maybe_vcpkg_recursive_data = get_environment_variable(EnvironmentVariableXVcpkgRecursiveData);
        if (auto vcpkg_recursive_data = maybe_vcpkg_recursive_data.get())
        {
            auto rec_doc = Json::parse(*vcpkg_recursive_data, EnvironmentVariableXVcpkgRecursiveData)
                               .value_or_exit(VCPKG_LINE_INFO)
                               .value;
            const auto& obj = rec_doc.object(VCPKG_LINE_INFO);

            if (auto entry = obj.get(JsonIdVcpkgRootArg))
            {
                auto as_sv = entry->string(VCPKG_LINE_INFO);
                args.vcpkg_root_dir_arg.emplace(as_sv.data(), as_sv.size());
            }

            if (auto entry = obj.get(JsonIdVcpkgRootEnv))
            {
                auto as_sv = entry->string(VCPKG_LINE_INFO);
                args.vcpkg_root_dir_env.emplace(as_sv.data(), as_sv.size());
            }

            if (auto entry = obj.get(JsonIdVcpkgDownloads))
            {
                auto as_sv = entry->string(VCPKG_LINE_INFO);
                args.downloads_root_dir.emplace(as_sv.data(), as_sv.size());
            }

            if (auto entry = obj.get(JsonIdVcpkgAssetSources))
            {
                auto as_sv = entry->string(VCPKG_LINE_INFO);
                args.asset_sources_template_env.emplace(as_sv.data(), as_sv.size());
            }

            if (obj.get(JsonIdVcpkgDisableMetrics))
            {
                args.disable_metrics = true;
            }

            args.do_not_take_lock = true;

            // Setting the recursive data to 'poison' prevents more than one level of recursion because
            // Json::parse() will fail.
            set_environment_variable(EnvironmentVariableXVcpkgRecursiveData, "poison");
        }
        else
        {
            Json::Object obj;
            if (auto vcpkg_root_dir_arg = args.vcpkg_root_dir_arg.get())
            {
                obj.insert(JsonIdVcpkgRootArg, Json::Value::string(*vcpkg_root_dir_arg));
            }

            if (auto vcpkg_root_dir_env = args.vcpkg_root_dir_env.get())
            {
                obj.insert(JsonIdVcpkgRootEnv, Json::Value::string(*vcpkg_root_dir_env));
            }

            if (auto downloads_root_dir = args.downloads_root_dir.get())
            {
                obj.insert(JsonIdVcpkgDownloads, Json::Value::string(*downloads_root_dir));
            }

            auto maybe_ast = args.asset_sources_template();
            if (auto ast = maybe_ast.get())
            {
                obj.insert(JsonIdVcpkgAssetSources, Json::Value::string(*ast));
            }

            if (args.disable_metrics)
            {
                obj.insert(JsonIdVcpkgDisableMetrics, Json::Value::boolean(true));
            }

            set_environment_variable(EnvironmentVariableXVcpkgRecursiveData,
                                     Json::stringify(obj, Json::JsonStyle::with_spaces(0)));
        }
    }

    void VcpkgCmdArguments::check_feature_flag_consistency() const
    {
        struct
        {
            StringView flag;
            StringView option;
            bool is_inconsistent;
        } possible_inconsistencies[] = {
            {FeatureFlagBinarycaching,
             SwitchBinarysource,
             !cli_binary_sources.empty() && !binary_caching.value_or(true)},
        };
        for (const auto& el : possible_inconsistencies)
        {
            if (el.is_inconsistent)
            {
                msg::write_unlocalized_text_to_stderr(Color::warning,
                                                      msg::format_warning(msgSpecifiedFeatureTurnedOff,
                                                                          msg::command_name = el.flag,
                                                                          msg::option = el.option)
                                                          .append_raw('\n')
                                                          .append_raw(WarningPrefix)
                                                          .append(msgDefaultFlag, msg::option = el.flag)
                                                          .append_raw('\n'));

                get_global_metrics_collector().track_string(StringMetric::Warning,
                                                            fmt::format("warning {} alongside {}", el.flag, el.option));
            }
        }
    }

    void VcpkgCmdArguments::debug_print_feature_flags() const
    {
        struct
        {
            StringView name;
            Optional<bool> flag;
        } flags[] = {
            {FeatureFlagBinarycaching, binary_caching},
            {FeatureFlagCompilertracking, compiler_tracking},
            {FeatureFlagRegistries, registries_feature},
            {FeatureFlagVersions, versions_feature},
            {FeatureFlagDependencygraph, dependency_graph_feature},
        };

        for (const auto& flag : flags)
        {
            if (auto r = flag.flag.get())
            {
                Debug::println("Feature flag '", flag.name, "' = ", *r ? "on" : "off");
            }
            else
            {
                Debug::println("Feature flag '", flag.name, "' unset");
            }
        }
    }

    void VcpkgCmdArguments::track_feature_flag_metrics() const
    {
        MetricsSubmission submission;
        submission.track_bool(BoolMetric::FeatureFlagBinaryCaching, binary_caching_enabled());
        submission.track_bool(BoolMetric::FeatureFlagCompilerTracking, compiler_tracking_enabled());
        submission.track_bool(BoolMetric::FeatureFlagDependencyGraph, dependency_graph_enabled());
        submission.track_bool(BoolMetric::FeatureFlagRegistries, registries_enabled());
        submission.track_bool(BoolMetric::FeatureFlagVersions, versions_enabled());
        get_global_metrics_collector().track_submission(std::move(submission));
    }

    void VcpkgCmdArguments::track_environment_metrics() const
    {
        MetricsSubmission submission;
        if (auto ci_env = m_detected_ci_environment_name.get())
        {
            Debug::println("Detected CI environment: ", *ci_env);
            submission.track_string(StringMetric::DetectedCiEnvironment, *ci_env);
        }

        if (auto repo_id = ci_repository_id.get())
        {
            submission.track_string(StringMetric::CiProjectId, *repo_id);
        }

        if (auto owner_id = ci_repository_owner_id.get())
        {
            submission.track_string(StringMetric::CiOwnerId, *owner_id);
        }

        get_global_metrics_collector().track_submission(std::move(submission));
    }

    Optional<std::string> VcpkgCmdArguments::asset_sources_template() const
    {
        std::string asset_sources_template = asset_sources_template_env.value_or("");
        if (auto ast = asset_sources_template_arg.get())
        {
            if (!asset_sources_template.empty()) asset_sources_template += ";";
            asset_sources_template += *ast;
        }
        if (asset_sources_template.empty()) return nullopt;
        return Optional<std::string>(std::move(asset_sources_template));
    }
}
