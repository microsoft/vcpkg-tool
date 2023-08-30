#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.new.h>
#include <vcpkg/configuration.h>
#include <vcpkg/registries.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versions.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral OPTION_APPLICATION = "application";
    constexpr StringLiteral OPTION_SINGLE_FILE = "single-file";

    constexpr StringLiteral SETTING_NAME = "name";
    constexpr StringLiteral SETTING_VERSION = "version";

    constexpr StringLiteral OPTION_VERSION_RELAXED = "version-relaxed";
    constexpr StringLiteral OPTION_VERSION_DATE = "version-date";
    constexpr StringLiteral OPTION_VERSION_STRING = "version-string";

    constexpr CommandSwitch SWITCHES[] = {
        {OPTION_APPLICATION, msgCmdNewOptApplication},
        {OPTION_SINGLE_FILE, msgCmdNewOptSingleFile},
        {OPTION_VERSION_RELAXED, msgCmdNewOptVersionRelaxed},
        {OPTION_VERSION_DATE, msgCmdNewOptVersionDate},
        {OPTION_VERSION_STRING, msgCmdNewOptVersionString},
    };

    constexpr CommandSetting SETTINGS[] = {
        {SETTING_NAME, msgCmdNewSettingName},
        {SETTING_VERSION, msgCmdNewSettingVersion},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandNewMetadata{
        "new",
        msgCmdNewSynposis,
        {msgCmdNewExample1, "vcpkg new --application"},
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

            manifest.insert("name", *name);
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
                    manifest.insert("version", *version);
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
                    manifest.insert("version-date", *version);
                }
                else
                {
                    return std::move(maybe_parsed).error();
                }
            }
            else if (option_version_string)
            {
                manifest.insert("version-string", *version);
            }
            else if (DateVersion::try_parse(*version).has_value())
            {
                manifest.insert("version-date", *version);
            }
            else if (DotVersion::try_parse_relaxed(*version).has_value())
            {
                manifest.insert("version", *version);
            }
            else
            {
                manifest.insert("version-string", *version);
            }
        }

        return std::move(manifest);
    }

    void command_new_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();
        const auto& current_configuration = paths.get_configuration();
        const auto parsed = args.parse_arguments(CommandNewMetadata);

        const bool option_application = Util::Sets::contains(parsed.switches, OPTION_APPLICATION);
        const bool option_single_file = Util::Sets::contains(parsed.switches, OPTION_SINGLE_FILE);
        const bool option_version_relaxed = Util::Sets::contains(parsed.switches, OPTION_VERSION_RELAXED);
        const bool option_version_date = Util::Sets::contains(parsed.switches, OPTION_VERSION_DATE);
        const bool option_version_string = Util::Sets::contains(parsed.switches, OPTION_VERSION_STRING);

        const std::string* name = parsed.read_setting(SETTING_NAME);
        const std::string* version = parsed.read_setting(SETTING_VERSION);

        auto maybe_manifest = build_prototype_manifest(
            name, version, option_application, option_version_relaxed, option_version_date, option_version_string);
        auto& manifest = maybe_manifest.value_or_exit(VCPKG_LINE_INFO);

        const auto almost_original_cwd = fs.almost_canonical(paths.original_cwd, VCPKG_LINE_INFO);
        const auto candidate_manifest_path = almost_original_cwd / "vcpkg.json";
        const auto candidate_configuration_path = almost_original_cwd / "vcpkg-configuration.json";
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
            case ConfigurationSource::ManifestFile:
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
                default_registry.kind.emplace("git");
                default_registry.repo.emplace(builtin_registry_git_url.data(), builtin_registry_git_url.size());
                default_registry.baseline.emplace(*current_builtin_sha);
                configuration.default_reg.emplace(std::move(default_registry));
            }
        }

        if (configuration.registries.empty())
        {
            // fill out default registries if there aren't any
            RegistryConfig default_ms_registry;
            default_ms_registry.kind.emplace("artifact");
            default_ms_registry.name.emplace("microsoft");
            default_ms_registry.location.emplace(
                "https://github.com/microsoft/vcpkg-ce-catalog/archive/refs/heads/main.zip");
            configuration.registries.emplace_back(std::move(default_ms_registry));
        }

        if (option_single_file)
        {
            manifest.insert("vcpkg-configuration", configuration.serialize());
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
