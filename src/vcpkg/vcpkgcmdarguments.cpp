#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.h>
#include <vcpkg/commands.integrate.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace
{
    using namespace vcpkg;

    constexpr std::pair<StringLiteral, StringLiteral> KNOWN_CI_VARIABLES[]{
        // Opt-out from CI detection
        {"VCPKG_NO_CI", "VCPKG_NO_CI"},

        // Azure Pipelines
        // https://docs.microsoft.com/en-us/azure/devops/pipelines/build/variables#system-variables
        {"TF_BUILD", "Azure_Pipelines"},

        // AppVeyor
        // https://www.appveyor.com/docs/environment-variables/
        {"APPVEYOR", "AppVeyor"},

        // AWS Code Build
        // https://docs.aws.amazon.com/codebuild/latest/userguide/build-env-ref-env-vars.html
        {"CODEBUILD_BUILD_ID", "AWS_CodeBuild"},

        // CircleCI
        // https://circleci.com/docs/env-vars#built-in-environment-variables
        {"CIRCLECI", "Circle_CI"},

        // GitHub Actions
        // https://docs.github.com/en/actions/learn-github-actions/
        {"GITHUB_ACTIONS", "GitHub_Actions"},

        // GitLab
        // https://docs.gitlab.com/ee/ci/variables/predefined_variables.html
        {"GITLAB_CI", "GitLab_CI"},

        // Heroku
        // https://devcenter.heroku.com/articles/heroku-ci#immutable-environment-variables
        {"HEROKU_TEST_RUN_ID", "Heroku_CI"},

        // Jenkins
        // https://wiki.jenkins.io/display/JENKINS/Building+a+software+project#Buildingasoftwareproject-belowJenkinsSetEnvironmentVariables
        {"JENKINS_URL", "Jenkins_CI"},

        // TeamCity
        // https://www.jetbrains.com/help/teamcity/predefined-build-parameters.html#Predefined+Server+Build+Parameters
        {"TEAMCITY_VERSION", "TeamCity_CI"},

        // Travis CI
        // https://docs.travis-ci.com/user/environment-variables/#default-environment-variables
        {"TRAVIS", "Travis_CI"},

        // Generic CI environment variables
        {"CI", "Generic"},
        {"BUILD_ID", "Generic"},
        {"BUILD_NUMBER", "Generic"},
    };
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
            StringView flag_name;
            Optional<bool>& local_option;
        };

        // Parsed for command line backcompat, but cannot be disabled
        Optional<bool> manifest_mode;
        const FeatureFlag flag_descriptions[] = {
            {VcpkgCmdArguments::BINARY_CACHING_FEATURE, args.binary_caching},
            {VcpkgCmdArguments::MANIFEST_MODE_FEATURE, manifest_mode},
            {VcpkgCmdArguments::COMPILER_TRACKING_FEATURE, args.compiler_tracking},
            {VcpkgCmdArguments::REGISTRIES_FEATURE, args.registries_feature},
            {VcpkgCmdArguments::VERSIONS_FEATURE, args.versions_feature},
        };

        for (const auto& desc : flag_descriptions)
        {
            set_from_feature_flag(flags, desc.flag_name, desc.local_option);
        }
    }

    static void parse_cojoined_value(StringView new_value, StringView option_name, Optional<std::string>& option_field)
    {
        if (option_field.has_value())
        {
            msg::println_error(msgDuplicateOptions, msg::value = option_name);
            print_usage();
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        option_field.emplace(new_value.data(), new_value.size());
    }

    static void parse_switch(bool new_setting, StringView option_name, Optional<bool>& option_field)
    {
        if (option_field && option_field != new_setting)
        {
            msg::println_error(msgConflictingValuesForOption, msg::option = option_name);
            print_usage();
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        option_field = new_setting;
    }

    static void parse_cojoined_multivalue(StringView new_value,
                                          StringView option_name,
                                          std::vector<std::string>& option_field)
    {
        if (new_value.size() == 0)
        {
            msg::println_error(msgExpectedValueForOption, msg::option = option_name);
            print_usage();
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        option_field.emplace_back(new_value.begin(), new_value.end());
    }

    static void parse_cojoined_list_multivalue(StringView new_value,
                                               StringView option_name,
                                               std::vector<std::string>& option_field)
    {
        if (new_value.size() == 0)
        {
            msg::println_error(msgExpectedValueForOption, msg::option = option_name);
            print_usage();
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        for (const auto& v : Strings::split(new_value, ','))
        {
            option_field.emplace_back(v.begin(), v.end());
        }
    }

    VcpkgCmdArguments VcpkgCmdArguments::create_from_command_line(const ILineReader& fs,
                                                                  const int argc,
                                                                  const CommandLineCharType* const* const argv)
    {
        std::vector<std::string> v = convert_argc_argv_to_arguments(argc, argv);
        replace_response_file_parameters(v, fs).value_or_exit(VCPKG_LINE_INFO);
        return VcpkgCmdArguments::create_from_arg_sequence(v.data(), v.data() + v.size());
    }

    enum class TryParseArgumentResult
    {
        NotFound,
        Found,
        FoundAndConsumedLookahead
    };

    template<class T, class F>
    static TryParseArgumentResult try_parse_argument_as_option(
        StringView arg, Optional<StringView> lookahead, StringView option, T& place, F parser)
    {
        if (Strings::starts_with(arg, "x-") && !Strings::starts_with(option, "x-"))
        {
            arg = arg.substr(2);
        }

        if (Strings::starts_with(arg, option))
        {
            if (arg.size() == option.size())
            {
                if (auto next = lookahead.get())
                {
                    parser(*next, option, place);
                    return TryParseArgumentResult::FoundAndConsumedLookahead;
                }

                msg::println_error(msgExpectedValueForOption, msg::option = option);
                print_usage();
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            if (arg[option.size()] == '=')
            {
                parser(arg.substr(option.size() + 1), option, place);
                return TryParseArgumentResult::Found;
            }
        }

        return TryParseArgumentResult::NotFound;
    }

    static bool equals_modulo_experimental(StringView arg, StringView option)
    {
        if (Strings::starts_with(arg, "x-") && !Strings::starts_with(option, "x-"))
        {
            return arg.substr(2) == option;
        }
        else
        {
            return arg == option;
        }
    }

    // returns true if this does parse this argument as this option
    template<class T>
    static bool try_parse_argument_as_switch(StringView option, StringView arg, T& place)
    {
        if (equals_modulo_experimental(arg, option))
        {
            parse_switch(true, option, place);
            return true;
        }

        if (Strings::starts_with(arg, "no-") && equals_modulo_experimental(arg.substr(3), option))
        {
            parse_switch(false, option, place);
            return true;
        }

        return false;
    }

    VcpkgCmdArguments VcpkgCmdArguments::create_from_arg_sequence(const std::string* arg_first,
                                                                  const std::string* arg_last)
    {
        VcpkgCmdArguments args;
        std::vector<std::string> feature_flags;

        if (arg_first != arg_last)
        {
            args.forwardable_arguments.assign(arg_first + 1, arg_last);
            if (arg_first + 1 == arg_last && *arg_first == "--version")
            {
                args.command = "version";
                return args;
            }
        }

        for (auto it = arg_first; it != arg_last; ++it)
        {
            std::string basic_arg = *it;

            if (basic_arg.empty())
            {
                continue;
            }

            if (basic_arg.size() >= 2 && basic_arg[0] == '-' && basic_arg[1] != '-')
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgUnsupportedShortOptions, msg::value = basic_arg);
            }

            if (basic_arg.size() < 2 || basic_arg[0] != '-')
            {
                if (args.command.empty())
                {
                    args.command = std::move(basic_arg);
                }
                else
                {
                    args.command_arguments.push_back(std::move(basic_arg));
                }
                continue;
            }

            // make argument case insensitive before the first =
            auto first_eq = Util::find(Span<char>(basic_arg), '=');
            Strings::ascii_to_lowercase(basic_arg.data(), first_eq);
            // basic_arg[0] == '-' && basic_arg[1] == '-'
            StringView arg = StringView(basic_arg).substr(2);
            constexpr static std::pair<StringView, Optional<std::string> VcpkgCmdArguments::*> cojoined_values[] = {
                {VCPKG_ROOT_DIR_ARG, &VcpkgCmdArguments::vcpkg_root_dir_arg},
                {TRIPLET_ARG, &VcpkgCmdArguments::triplet},
                {HOST_TRIPLET_ARG, &VcpkgCmdArguments::host_triplet},
                {MANIFEST_ROOT_DIR_ARG, &VcpkgCmdArguments::manifest_root_dir},
                {BUILDTREES_ROOT_DIR_ARG, &VcpkgCmdArguments::buildtrees_root_dir},
                {DOWNLOADS_ROOT_DIR_ARG, &VcpkgCmdArguments::downloads_root_dir},
                {INSTALL_ROOT_DIR_ARG, &VcpkgCmdArguments::install_root_dir},
                {PACKAGES_ROOT_DIR_ARG, &VcpkgCmdArguments::packages_root_dir},
                {SCRIPTS_ROOT_DIR_ARG, &VcpkgCmdArguments::scripts_root_dir},
                {BUILTIN_PORTS_ROOT_DIR_ARG, &VcpkgCmdArguments::builtin_ports_root_dir},
                {BUILTIN_REGISTRY_VERSIONS_DIR_ARG, &VcpkgCmdArguments::builtin_registry_versions_dir},
                {REGISTRIES_CACHE_DIR_ARG, &VcpkgCmdArguments::registries_cache_dir},
                {ASSET_SOURCES_ARG, &VcpkgCmdArguments::asset_sources_template_arg},
            };

            constexpr static std::pair<StringView, std::vector<std::string> VcpkgCmdArguments::*>
                cojoined_multivalues[] = {
                    {OVERLAY_PORTS_ARG, &VcpkgCmdArguments::cli_overlay_ports},
                    {OVERLAY_TRIPLETS_ARG, &VcpkgCmdArguments::cli_overlay_triplets},
                    {BINARY_SOURCES_ARG, &VcpkgCmdArguments::binary_sources},
                    {CMAKE_SCRIPT_ARG, &VcpkgCmdArguments::cmake_args},
                };

            constexpr static std::pair<StringView, Optional<bool> VcpkgCmdArguments::*> switches[] = {
                {DEBUG_SWITCH, &VcpkgCmdArguments::debug},
                {DEBUG_ENV_SWITCH, &VcpkgCmdArguments::debug_env},
                {DISABLE_METRICS_SWITCH, &VcpkgCmdArguments::disable_metrics},
                {SEND_METRICS_SWITCH, &VcpkgCmdArguments::send_metrics},
                {PRINT_METRICS_SWITCH, &VcpkgCmdArguments::print_metrics},
                {FEATURE_PACKAGES_SWITCH, &VcpkgCmdArguments::feature_packages},
                {BINARY_CACHING_SWITCH, &VcpkgCmdArguments::binary_caching},
                {WAIT_FOR_LOCK_SWITCH, &VcpkgCmdArguments::wait_for_lock},
                {IGNORE_LOCK_FAILURES_SWITCH, &VcpkgCmdArguments::ignore_lock_failures},
                {JSON_SWITCH, &VcpkgCmdArguments::json},
                {EXACT_ABI_TOOLS_VERSIONS_SWITCH, &VcpkgCmdArguments::exact_abi_tools_versions},
            };

            Optional<StringView> lookahead;
            if (it + 1 != arg_last)
            {
                lookahead = it[1];
            }

            bool found = false;
            for (const auto& pr : cojoined_values)
            {
                switch (try_parse_argument_as_option(arg, lookahead, pr.first, args.*pr.second, parse_cojoined_value))
                {
                    case TryParseArgumentResult::FoundAndConsumedLookahead: ++it; [[fallthrough]];
                    case TryParseArgumentResult::Found: found = true; break;
                    case TryParseArgumentResult::NotFound: break;
                    default: Checks::unreachable(VCPKG_LINE_INFO);
                }
            }
            if (found) continue;

            for (const auto& pr : cojoined_multivalues)
            {
                switch (
                    try_parse_argument_as_option(arg, lookahead, pr.first, args.*pr.second, parse_cojoined_multivalue))
                {
                    case TryParseArgumentResult::FoundAndConsumedLookahead: ++it; [[fallthrough]];
                    case TryParseArgumentResult::Found: found = true; break;
                    case TryParseArgumentResult::NotFound: break;
                    default: Checks::unreachable(VCPKG_LINE_INFO);
                }
            }
            if (found) continue;

            switch (try_parse_argument_as_option(
                arg, lookahead, FEATURE_FLAGS_ARG, feature_flags, parse_cojoined_list_multivalue))
            {
                case TryParseArgumentResult::FoundAndConsumedLookahead: ++it; [[fallthrough]];
                case TryParseArgumentResult::Found: found = true; break;
                case TryParseArgumentResult::NotFound: break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }

            for (const auto& pr : switches)
            {
                if (try_parse_argument_as_switch(pr.first, arg, args.*pr.second))
                {
                    found = true;
                    break;
                }
            }
            if (found) continue;

            const auto eq_pos = std::find(arg.begin(), arg.end(), '=');
            if (eq_pos != arg.end())
            {
                const auto& key = StringView(arg.begin(), eq_pos);
                const auto& value = StringView(eq_pos + 1, arg.end());

                args.command_options[key.to_string()].push_back(value.to_string());
            }
            else
            {
                args.command_switches.insert(arg.to_string());
            }
        }

        parse_feature_flags(feature_flags, args);

        // --debug-env implies --debug
        if (const auto p = args.debug_env.get()) args.debug_env = *p;

        return args;
    }

    ParsedArguments VcpkgCmdArguments::parse_arguments(const CommandStructure& command_structure) const
    {
        bool failed = false;
        ParsedArguments output;

        const size_t actual_arg_count = command_arguments.size();

        if (command_structure.minimum_arity == command_structure.maximum_arity)
        {
            if (actual_arg_count != command_structure.minimum_arity)
            {
                msg::println_error(msgIncorrectNumberOfArgs,
                                   msg::command_name = this->command,
                                   msg::expected = command_structure.minimum_arity,
                                   msg::actual = actual_arg_count);
                failed = true;
            }
        }
        else
        {
            if (actual_arg_count < command_structure.minimum_arity)
            {
                msg::println_error(msgIncorrectNumberOfArgs,
                                   msg::command_name = this->command,
                                   msg::expected = command_structure.minimum_arity,
                                   msg::actual = actual_arg_count);
                failed = true;
            }
            if (actual_arg_count > command_structure.maximum_arity)
            {
                msg::println_error(msgIncorrectNumberOfArgs,
                                   msg::command_name = this->command,
                                   msg::expected = command_structure.minimum_arity,
                                   msg::actual = actual_arg_count);
                failed = true;
            }
        }

        auto switches_copy = this->command_switches;
        auto options_copy = this->command_options;

        const auto find_option = [](const auto& set, StringLiteral name) {
            auto it = set.find(name);
            if (it == set.end() && !Strings::starts_with(name, "x-"))
            {
                it = set.find(Strings::format("x-%s", name));
            }

            return it;
        };

        for (const auto& switch_ : command_structure.options.switches)
        {
            const auto it = find_option(switches_copy, switch_.name);
            if (it != switches_copy.end())
            {
                output.switches.insert(switch_.name.to_string());
                switches_copy.erase(it);
            }
            const auto option_it = find_option(options_copy, switch_.name);
            if (option_it != options_copy.end())
            {
                // This means that the switch was passed like '--a=xyz'
                msg::println_error(msgNoArgumentsForOption, msg::option = switch_.name);
                options_copy.erase(option_it);
                failed = true;
            }
        }

        for (const auto& option : command_structure.options.settings)
        {
            const auto it = find_option(options_copy, option.name);
            if (it != options_copy.end())
            {
                const auto& value = it->second;
                if (value.empty())
                {
                    Checks::unreachable(VCPKG_LINE_INFO);
                }

                if (value.size() > 1)
                {
                    msg::println_error(msgDuplicateCommandOption, msg::option = option.name);
                    failed = true;
                }
                else if (value.front().empty())
                {
                    // Fail when not given a value, e.g.: "vcpkg install sqlite3 --additional-ports="
                    msg::println_error(msgEmptyArg, msg::option = option.name);
                    failed = true;
                }
                else
                {
                    output.settings.emplace(option.name, value.front());
                    options_copy.erase(it);
                }
            }
            const auto switch_it = find_option(switches_copy, option.name);
            if (switch_it != switches_copy.end())
            {
                // This means that the option was passed like '--a'
                msg::println_error(msgEmptyArg, msg::option = option.name);
                switches_copy.erase(switch_it);
                failed = true;
            }
        }

        for (const auto& option : command_structure.options.multisettings)
        {
            const auto it = find_option(options_copy, option.name);
            if (it != options_copy.end())
            {
                const auto& value = it->second;
                for (const auto& v : value)
                {
                    if (v.empty())
                    {
                        msg::println_error(msgEmptyArg, msg::option = option.name);
                        failed = true;
                    }
                    else
                    {
                        output.multisettings[option.name.to_string()].push_back(v);
                    }
                }
                options_copy.erase(it);
            }
            const auto switch_it = find_option(switches_copy, option.name);
            if (switch_it != switches_copy.end())
            {
                // This means that the option was passed like '--a'
                msg::println_error(msgEmptyArg, msg::option = option.name);
                switches_copy.erase(switch_it);
                failed = true;
            }
        }

        if (!switches_copy.empty() || !options_copy.empty())
        {
            auto message = msg::format(msgUnknownOptions, msg::command_name = this->command);
            for (auto&& switch_ : switches_copy)
            {
                message.append_indent().append_raw("\'--" + switch_ + "\'\n");
            }
            for (auto&& option : options_copy)
            {
                message.append_indent().append_raw("\'--" + option.first + "\'\n");
            }

            failed = true;
        }

        if (failed)
        {
            print_usage(command_structure);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        return output;
    }

    const std::vector<std::string>& VcpkgCmdArguments::get_forwardable_arguments() const noexcept
    {
        return forwardable_arguments;
    }

    void print_usage()
    {
        HelpTableFormatter table;
        table.header("Commands");
        table.format("vcpkg search [pat]", msg::format(msgHelpSearchCommand));
        table.format("vcpkg install <pkg>...", msg::format(msgHelpInstallCommand));
        table.format("vcpkg remove <pkg>...", msg::format(msgHelpRemoveCommand));
        table.format("vcpkg update", msg::format(msgHelpUpdateCommand));
        table.format("vcpkg remove --outdated", msg::format(msgHelpRemoveOutdatedCommand));
        table.format("vcpkg upgrade", msg::format(msgHelpUpgradeCommand));
        table.format("vcpkg hash <file> [alg]", msg::format(msgHelpHashCommand));
        table.format("vcpkg help topics", msg::format(msgHelpTopicsCommand));
        table.format("vcpkg help <topic>", msg::format(msgHelpTopicCommand));
        table.format("vcpkg list", msg::format(msgHelpListCommand));
        table.blank();
        Commands::Integrate::append_helpstring(table);
        table.blank();
        table.format("vcpkg export <pkg>... [opt]...", msg::format(msgHelpExportCommand));
        table.format("vcpkg edit <pkg>", msg::format(msgHelpEditCommand, msg::env_var = "EDITOR"));
        table.format("vcpkg create <pkg> <url> [archivename]", msg::format(msgHelpCreateCommand));
        table.format("vcpkg x-init-registry <path>", msg::format(msgHelpInitializeRegistryCommand));
        table.format("vcpkg format-manifest --all", msg::format(msgHelpFormatManifestCommand));
        table.format("vcpkg owns <pat>", msg::format(msgHelpOwnsCommand));
        table.format("vcpkg depend-info <pkg>...", msg::format(msgHelpDependInfoCommand));
        table.format("vcpkg env", msg::format(msgHelpEnvCommand));
        table.format("vcpkg version", msg::format(msgHelpVersionCommand));
        table.format("vcpkg contact", msg::format(msgHelpContactCommand));
        table.blank();
        table.header("Options");
        VcpkgCmdArguments::append_common_options(table);
        table.blank();
        table.format("@response_file", msg::format(msgHelpResponseFileCommand));
        table.blank();
        table.example(msg::format(msgHelpExampleCommand));

        msg::println(LocalizedString::from_raw(table.m_str));
    }

    void print_usage(const CommandStructure& command_structure)
    {
        HelpTableFormatter table;
        if (!command_structure.example_text.empty())
        {
            table.example(command_structure.example_text);
        }

        table.header("Options");
        for (auto&& option : command_structure.options.switches)
        {
            auto helpmsg = option.helpmsg;
            if (helpmsg)
            {
                table.format(Strings::format("--%s", option.name), helpmsg());
            }
        }
        for (auto&& option : command_structure.options.settings)
        {
            auto helpmsg = option.helpmsg;
            if (helpmsg)
            {
                table.format(Strings::format("--%s=...", option.name), helpmsg());
            }
        }
        for (auto&& option : command_structure.options.multisettings)
        {
            auto helpmsg = option.helpmsg;
            if (helpmsg)
            {
                table.format(Strings::format("--%s=...", option.name), helpmsg());
            }
        }

        VcpkgCmdArguments::append_common_options(table);
        msg::println(LocalizedString::from_raw(table.m_str));
    }

    void VcpkgCmdArguments::append_common_options(HelpTableFormatter& table)
    {
        static auto opt = [](StringView arg, StringView joiner, StringView value) {
            return Strings::concat("--", arg, joiner, value);
        };

        table.format(opt(TRIPLET_ARG, "=", "<t>"),
                     msg::format(msgSpecifyTargetArch, msg::env_var = "VCPKG_DEFAULT_TRIPLET"));
        table.format(opt(HOST_TRIPLET_ARG, "=", "<t>"),
                     msg::format(msgSpecifyHostArch, msg::env_var = "VCPKG_DEFAULT_HOST_TRIPLET"));
        table.format(opt(OVERLAY_PORTS_ARG, "=", "<path>"),
                     msg::format(msgSpecifyDirectoriesWhenSearching, msg::env_var = "VCPKG_OVERLAY_PORTS"));
        table.format(opt(OVERLAY_TRIPLETS_ARG, "=", "<path>"),
                     msg::format(msgSpecifyDirectoriesContaining, msg::env_var = "VCPKG_OVERLAY_TRIPLETS"));
        table.format(opt(BINARY_SOURCES_ARG, "=", "<path>"), msg::format(msgBinarySourcesArg));
        table.format(opt(ASSET_SOURCES_ARG, "=", "<path>"), msg::format(msgAssetSourcesArg));
        table.format(opt(DOWNLOADS_ROOT_DIR_ARG, "=", "<path>"),
                     msg::format(msgDownloadRootsDir, msg::env_var = "VCPKG_DOWNLOADS"));
        table.format(opt(VCPKG_ROOT_DIR_ARG, "=", "<path>"),
                     msg::format(msgVcpkgRootsDir, msg::env_var = "VCPKG_ROOT"));
        table.format(opt(BUILDTREES_ROOT_DIR_ARG, "=", "<path>"), msg::format(msgBuildTreesRootDir));
        table.format(opt(INSTALL_ROOT_DIR_ARG, "=", "<path>"), msg::format(msgInstallRootDir));
        table.format(opt(PACKAGES_ROOT_DIR_ARG, "=", "<path>"), msg::format(msgPackageRootDir));
        table.format(opt(JSON_SWITCH, "", ""), msg::format(msgJsonSwitch));
    }

    static void from_env(const std::function<Optional<std::string>(ZStringView)>& f,
                         ZStringView var,
                         Optional<std::string>& dst)
    {
        if (dst) return;

        dst = f(var);
    }

    void VcpkgCmdArguments::imbue_from_environment() { imbue_from_environment_impl(&vcpkg::get_environment_variable); }
    void VcpkgCmdArguments::imbue_from_fake_environment(const std::map<std::string, std::string, std::less<>>& env)
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
            const auto vcpkg_disable_metrics_env = get_env(DISABLE_METRICS_ENV);
            if (vcpkg_disable_metrics_env.has_value())
            {
                disable_metrics = true;
            }
        }

        from_env(get_env, TRIPLET_ENV, triplet);
        from_env(get_env, HOST_TRIPLET_ENV, host_triplet);
        vcpkg_root_dir_env = get_environment_variable(VCPKG_ROOT_DIR_ENV);
        from_env(get_env, DOWNLOADS_ROOT_DIR_ENV, downloads_root_dir);
        from_env(get_env, DEFAULT_VISUAL_STUDIO_PATH_ENV, default_visual_studio_path);
        from_env(get_env, ASSET_SOURCES_ENV, asset_sources_template_env);
        from_env(get_env, REGISTRIES_CACHE_DIR_ENV, registries_cache_dir);

        // detect whether we are running in a CI environment
        for (auto&& ci_env_var : KNOWN_CI_VARIABLES)
        {
            if (get_env(ci_env_var.first).has_value())
            {
                m_detected_ci_environment = ci_env_var.second;
                break;
            }
        }

        {
            const auto vcpkg_disable_lock = get_env(IGNORE_LOCK_FAILURES_ENV);
            if (vcpkg_disable_lock.has_value() && !ignore_lock_failures.has_value())
            {
                ignore_lock_failures = true;
            }
        }

        {
            const auto vcpkg_overlay_ports_env = get_env(OVERLAY_PORTS_ENV);
            if (const auto unpacked = vcpkg_overlay_ports_env.get())
            {
                env_overlay_ports = Strings::split_paths(*unpacked);
            }
        }
        {
            const auto vcpkg_overlay_triplets_env = get_env(OVERLAY_TRIPLETS_ENV);
            if (const auto unpacked = vcpkg_overlay_triplets_env.get())
            {
                env_overlay_triplets = Strings::split_paths(*unpacked);
            }
        }
        {
            const auto vcpkg_feature_flags_env = get_env(FEATURE_FLAGS_ENV);
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

        auto maybe_vcpkg_recursive_data = get_environment_variable(RECURSIVE_DATA_ENV);
        if (auto vcpkg_recursive_data = maybe_vcpkg_recursive_data.get())
        {
            auto rec_doc = Json::parse(*vcpkg_recursive_data).value_or_exit(VCPKG_LINE_INFO).first;
            const auto& obj = rec_doc.object(VCPKG_LINE_INFO);

            if (auto entry = obj.get(VCPKG_ROOT_ARG_NAME))
            {
                auto as_sv = entry->string(VCPKG_LINE_INFO);
                args.vcpkg_root_dir_arg.emplace(as_sv.data(), as_sv.size());
            }

            if (auto entry = obj.get(VCPKG_ROOT_ENV_NAME))
            {
                auto as_sv = entry->string(VCPKG_LINE_INFO);
                args.vcpkg_root_dir_env.emplace(as_sv.data(), as_sv.size());
            }

            if (auto entry = obj.get(DOWNLOADS_ROOT_DIR_ENV))
            {
                auto as_sv = entry->string(VCPKG_LINE_INFO);
                args.downloads_root_dir.emplace(as_sv.data(), as_sv.size());
            }

            if (auto entry = obj.get(ASSET_SOURCES_ENV))
            {
                auto as_sv = entry->string(VCPKG_LINE_INFO);
                args.asset_sources_template_env.emplace(as_sv.data(), as_sv.size());
            }

            if (obj.get(DISABLE_METRICS_ENV))
            {
                args.disable_metrics = true;
            }

            args.do_not_take_lock = true;

            // Setting the recursive data to 'poison' prevents more than one level of recursion because
            // Json::parse() will fail.
            set_environment_variable(RECURSIVE_DATA_ENV, "poison");
        }
        else
        {
            Json::Object obj;
            if (auto vcpkg_root_dir_arg = args.vcpkg_root_dir_arg.get())
            {
                obj.insert(VCPKG_ROOT_ARG_NAME, Json::Value::string(*vcpkg_root_dir_arg));
            }

            if (auto vcpkg_root_dir_env = args.vcpkg_root_dir_env.get())
            {
                obj.insert(VCPKG_ROOT_ENV_NAME, Json::Value::string(*vcpkg_root_dir_env));
            }

            if (auto downloads_root_dir = args.downloads_root_dir.get())
            {
                obj.insert(DOWNLOADS_ROOT_DIR_ENV, Json::Value::string(*downloads_root_dir));
            }

            auto maybe_ast = args.asset_sources_template();
            if (auto ast = maybe_ast.get())
            {
                obj.insert(ASSET_SOURCES_ENV, Json::Value::string(*ast));
            }

            if (args.disable_metrics)
            {
                obj.insert(DISABLE_METRICS_ENV, Json::Value::boolean(true));
            }

            set_environment_variable(RECURSIVE_DATA_ENV, Json::stringify(obj, Json::JsonStyle::with_spaces(0)));
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
            {BINARY_CACHING_FEATURE, BINARY_SOURCES_ARG, !binary_sources.empty() && !binary_caching.value_or(true)},
        };
        for (const auto& el : possible_inconsistencies)
        {
            if (el.is_inconsistent)
            {
                msg::println_warning(
                    msgSpecifiedFeatureTurnedOff, msg::command_name = el.flag, msg::option = el.option);
                msg::println_warning(msgDefaultFlag, msg::option = el.flag);
                get_global_metrics_collector().track_string(
                    StringMetric::Warning, Strings::format("warning %s alongside %s", el.flag, el.option));
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
            {BINARY_CACHING_FEATURE, binary_caching},
            {COMPILER_TRACKING_FEATURE, compiler_tracking},
            {REGISTRIES_FEATURE, registries_feature},
            {VERSIONS_FEATURE, versions_feature},
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
        submission.track_bool(BoolMetric::FeatureFlagRegistries, registries_enabled());
        submission.track_bool(BoolMetric::FeatureFlagVersions, versions_enabled());
        get_global_metrics_collector().track_submission(std::move(submission));
    }

    void VcpkgCmdArguments::track_environment_metrics() const
    {
        if (auto ci_env = m_detected_ci_environment.get())
        {
            Debug::println("Detected CI environment: ", *ci_env);
            get_global_metrics_collector().track_string(StringMetric::DetectedCiEnvironment, *ci_env);
        }
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

    std::string create_example_string(const std::string& command_and_arguments)
    {
        std::string cs = Strings::format("Example:\n"
                                         "  vcpkg %s\n",
                                         command_and_arguments);
        return cs;
    }

    // out-of-line definitions since C++14 doesn't allow inline constexpr static variables
    constexpr StringLiteral VcpkgCmdArguments::VCPKG_ROOT_DIR_ENV;
    constexpr StringLiteral VcpkgCmdArguments::VCPKG_ROOT_DIR_ARG;
    constexpr StringLiteral VcpkgCmdArguments::MANIFEST_ROOT_DIR_ARG;

    constexpr StringLiteral VcpkgCmdArguments::BUILDTREES_ROOT_DIR_ARG;
    constexpr StringLiteral VcpkgCmdArguments::DOWNLOADS_ROOT_DIR_ENV;
    constexpr StringLiteral VcpkgCmdArguments::DOWNLOADS_ROOT_DIR_ARG;
    constexpr StringLiteral VcpkgCmdArguments::INSTALL_ROOT_DIR_ARG;
    constexpr StringLiteral VcpkgCmdArguments::PACKAGES_ROOT_DIR_ARG;
    constexpr StringLiteral VcpkgCmdArguments::SCRIPTS_ROOT_DIR_ARG;
    constexpr StringLiteral VcpkgCmdArguments::BUILTIN_PORTS_ROOT_DIR_ARG;
    constexpr StringLiteral VcpkgCmdArguments::BUILTIN_REGISTRY_VERSIONS_DIR_ARG;

    constexpr StringLiteral VcpkgCmdArguments::DEFAULT_VISUAL_STUDIO_PATH_ENV;

    constexpr StringLiteral VcpkgCmdArguments::TRIPLET_ENV;
    constexpr StringLiteral VcpkgCmdArguments::TRIPLET_ARG;
    constexpr StringLiteral VcpkgCmdArguments::HOST_TRIPLET_ENV;
    constexpr StringLiteral VcpkgCmdArguments::HOST_TRIPLET_ARG;
    constexpr StringLiteral VcpkgCmdArguments::OVERLAY_PORTS_ENV;
    constexpr StringLiteral VcpkgCmdArguments::OVERLAY_PORTS_ARG;
    constexpr StringLiteral VcpkgCmdArguments::OVERLAY_TRIPLETS_ENV;
    constexpr StringLiteral VcpkgCmdArguments::OVERLAY_TRIPLETS_ARG;

    constexpr StringLiteral VcpkgCmdArguments::BINARY_SOURCES_ARG;

    constexpr StringLiteral VcpkgCmdArguments::DEBUG_SWITCH;
    constexpr StringLiteral VcpkgCmdArguments::DEBUG_ENV_SWITCH;
    constexpr StringLiteral VcpkgCmdArguments::SEND_METRICS_SWITCH;
    constexpr StringLiteral VcpkgCmdArguments::DISABLE_METRICS_ENV;
    constexpr StringLiteral VcpkgCmdArguments::DISABLE_METRICS_SWITCH;
    constexpr StringLiteral VcpkgCmdArguments::PRINT_METRICS_SWITCH;

    constexpr StringLiteral VcpkgCmdArguments::WAIT_FOR_LOCK_SWITCH;
    constexpr StringLiteral VcpkgCmdArguments::IGNORE_LOCK_FAILURES_SWITCH;
    constexpr StringLiteral VcpkgCmdArguments::IGNORE_LOCK_FAILURES_ENV;

    constexpr StringLiteral VcpkgCmdArguments::JSON_SWITCH;

    constexpr StringLiteral VcpkgCmdArguments::ASSET_SOURCES_ENV;
    constexpr StringLiteral VcpkgCmdArguments::ASSET_SOURCES_ARG;

    constexpr StringLiteral VcpkgCmdArguments::FEATURE_FLAGS_ENV;
    constexpr StringLiteral VcpkgCmdArguments::FEATURE_FLAGS_ARG;

    constexpr StringLiteral VcpkgCmdArguments::FEATURE_PACKAGES_SWITCH;
    constexpr StringLiteral VcpkgCmdArguments::BINARY_CACHING_FEATURE;
    constexpr StringLiteral VcpkgCmdArguments::BINARY_CACHING_SWITCH;
    constexpr StringLiteral VcpkgCmdArguments::COMPILER_TRACKING_FEATURE;
    constexpr StringLiteral VcpkgCmdArguments::MANIFEST_MODE_FEATURE;
    constexpr StringLiteral VcpkgCmdArguments::REGISTRIES_FEATURE;
    constexpr StringLiteral VcpkgCmdArguments::RECURSIVE_DATA_ENV;
    constexpr StringLiteral VcpkgCmdArguments::VERSIONS_FEATURE;

    constexpr StringLiteral VcpkgCmdArguments::CMAKE_SCRIPT_ARG;
    constexpr StringLiteral VcpkgCmdArguments::EXACT_ABI_TOOLS_VERSIONS_SWITCH;
}
