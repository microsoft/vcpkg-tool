#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.new.h>
#include <vcpkg/configuration.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versions.h>

using namespace vcpkg;

namespace
{
    constexpr CommandSwitch SWITCHES[] = {
        {SwitchApplication, msgCmdNewOptApplication},
        {SwitchSingleFile, msgCmdNewOptSingleFile},
        {SwitchVersionRelaxed, msgCmdNewOptVersionRelaxed},
        {SwitchVersionDate, msgCmdNewOptVersionDate},
        {SwitchVersionString, msgCmdNewOptVersionString},
    };

    constexpr CommandSetting SETTINGS[] = {
        {SwitchName, msgCmdNewSettingName},
        {SwitchVersion, msgCmdNewSettingVersion},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandNewMetadata{
        "new",
        msgCmdNewSynposis,
        {msgCmdNewExample1, "vcpkg new --application"},
        Undocumented,
        AutocompletePriority::Public,
        0,
        0,
        {SWITCHES, SETTINGS},
        nullptr,
    };

    ExpectedL<Json::Object> build_prototype_manifest(const std::string* name,
                                                     const std::string* version,
                                                     bool option_application,
                                                     bool option_version_relaxed,
                                                     bool option_version_date,
                                                     bool option_version_string)
    {
        if (!Util::zero_or_one_set(option_version_relaxed, option_version_date, option_version_string))
        {
            return msg::format_error(msgNewOnlyOneVersionKind);
        }

        if (!option_application && (!name || !version))
        {
            return msg::format_error(msgNewSpecifyNameVersionOrApplication);
        }

        auto manifest = Json::Object();
        if (name)
        {
            if (name->empty())
            {
                return msg::format_error(msgNewNameCannotBeEmpty);
            }

            if (!Json::IdentifierDeserializer::is_ident(*name))
            {
                return msg::format_error(msgParseIdentifierError,
                                         msg::value = *name,
                                         msg::url = "https://learn.microsoft.com/vcpkg/commands/new");
            }

            manifest.insert(JsonIdName, *name);
        }

        if (version)
        {
            if (version->empty())
            {
                return msg::format_error(msgNewVersionCannotBeEmpty);
            }

            if (option_version_relaxed)
            {
                auto maybe_parsed = DotVersion::try_parse_relaxed(*version);
                if (maybe_parsed.has_value())
                {
                    manifest.insert(JsonIdVersion, *version);
                }
                else
                {
                    return std::move(maybe_parsed).error();
                }
            }
            else if (option_version_date)
            {
                auto maybe_parsed = DateVersion::try_parse(*version);
                if (maybe_parsed.has_value())
                {
                    manifest.insert(JsonIdVersionDate, *version);
                }
                else
                {
                    return std::move(maybe_parsed).error();
                }
            }
            else if (option_version_string)
            {
                manifest.insert(JsonIdVersionString, *version);
            }
            else if (DateVersion::try_parse(*version).has_value())
            {
                manifest.insert(JsonIdVersionDate, *version);
            }
            else if (DotVersion::try_parse_relaxed(*version).has_value())
            {
                manifest.insert(JsonIdVersion, *version);
            }
            else
            {
                manifest.insert(JsonIdVersionString, *version);
            }
        }

        return std::move(manifest);
    }

    void command_new_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();
        const auto& current_configuration = paths.get_configuration();
        const auto parsed = args.parse_arguments(CommandNewMetadata);

        const bool option_application = Util::Sets::contains(parsed.switches, SwitchApplication);
        const bool option_single_file = Util::Sets::contains(parsed.switches, SwitchSingleFile);
        const bool option_version_relaxed = Util::Sets::contains(parsed.switches, SwitchVersionRelaxed);
        const bool option_version_date = Util::Sets::contains(parsed.switches, SwitchVersionDate);
        const bool option_version_string = Util::Sets::contains(parsed.switches, SwitchVersionString);

        const std::string* name = parsed.read_setting(SwitchName);
        const std::string* version = parsed.read_setting(SwitchVersion);

        auto maybe_manifest = build_prototype_manifest(
            name, version, option_application, option_version_relaxed, option_version_date, option_version_string);
        auto& manifest = maybe_manifest.value_or_exit(VCPKG_LINE_INFO);

        const auto almost_original_cwd = fs.almost_canonical(paths.original_cwd, VCPKG_LINE_INFO);
        const auto candidate_manifest_path = almost_original_cwd / FileVcpkgDotJson;
        const auto candidate_configuration_path = almost_original_cwd / FileVcpkgConfigurationDotJson;
        if (fs.exists(candidate_manifest_path, VCPKG_LINE_INFO))
        {
            Checks::msg_exit_with_error(
                VCPKG_LINE_INFO, msgNewManifestAlreadyExists, msg::path = candidate_manifest_path);
        }

        if (fs.exists(candidate_configuration_path, VCPKG_LINE_INFO))
        {
            Checks::msg_exit_with_error(
                VCPKG_LINE_INFO, msgNewConfigurationAlreadyExists, msg::path = candidate_configuration_path);
        }

        Configuration configuration;
        switch (current_configuration.source)
        {
            case ConfigurationSource::None:
                // no existing configuration, so create one with out-of-the-box registries
                break;
            case ConfigurationSource::VcpkgConfigurationFile:
            case ConfigurationSource::ManifestFileVcpkgConfiguration:
            case ConfigurationSource::ManifestFileConfiguration:
                // reuse existing configuration
                configuration = current_configuration.config;
                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        if (!configuration.default_reg.has_value())
        {
            // fill out the default baseline if we don't have one yet
            auto maybe_current_builtin_sha = paths.get_current_git_sha();
            if (auto current_builtin_sha = maybe_current_builtin_sha.get())
            {
                RegistryConfig default_registry;
                default_registry.kind.emplace(JsonIdGit);
                default_registry.repo.emplace(builtin_registry_git_url.data(), builtin_registry_git_url.size());
                default_registry.baseline.emplace(*current_builtin_sha);
                configuration.default_reg.emplace(std::move(default_registry));
            }
        }

        if (configuration.registries.empty())
        {
            // fill out default registries if there aren't any
            RegistryConfig default_ms_registry;
            default_ms_registry.kind.emplace(JsonIdArtifact);
            default_ms_registry.name.emplace(JsonIdMicrosoft);
            default_ms_registry.location.emplace(
                "https://github.com/microsoft/vcpkg-ce-catalog/archive/refs/heads/main.zip");
            configuration.registries.emplace_back(std::move(default_ms_registry));
        }

        if (option_single_file)
        {
            manifest.insert(JsonIdVcpkgConfiguration, configuration.serialize());
        }
        else
        {
            fs.write_contents(
                candidate_configuration_path, Json::stringify(configuration.serialize()), VCPKG_LINE_INFO);
        }

        fs.write_contents(candidate_manifest_path, Json::stringify(manifest), VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
