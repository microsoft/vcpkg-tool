#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>

#include <vcpkg/commands.update-baseline.h>
#include <vcpkg/configuration.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    namespace msg = vcpkg::msg;
    DECLARE_AND_REGISTER_MESSAGE(UpdateBaselineNoConfiguration, (), "", "There is no configuration file to update.");
    DECLARE_AND_REGISTER_MESSAGE(UpdateBaselineGitError,
                                 (msg::url),
                                 "",
                                 "Git failed to fetch remote repository {url}:");
}

namespace vcpkg::Commands
{
    static const CommandStructure COMMAND_STRUCTURE{
        create_example_string("x-update-baseline"),
        0,
        0,
    };

    static constexpr StringLiteral builtin_git_url = "https://github.com/microsoft/vcpkg";

    static Optional<std::string> get_baseline_from_git_repo(const VcpkgPaths& paths, StringView url)
    {
        auto res = paths.git_fetch_from_remote_registry(url, "HEAD");
        if (res.has_value())
        {
            return std::move(*res.get());
        }
        else
        {
            msg::println(Color::warning, msgUpdateBaselineGitError, msg::url = url);
            msg::write_unlocalized_text_to_stdout(Color::warning, res.error());
            return nullopt;
        }
    }

    static Optional<std::string> get_latest_baseline(const VcpkgPaths& paths, const RegistryConfig& r)
    {
        if (r.kind == "git")
        {
            return get_baseline_from_git_repo(paths, r.repo.value_or_exit(VCPKG_LINE_INFO));
        }
        else if (r.kind == "builtin")
        {
            return get_baseline_from_git_repo(paths, builtin_git_url);
        }
        else
        {
            return nullopt;
        }
    }

    void UpdateBaselineCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        args.parse_arguments(COMMAND_STRUCTURE);

        bool has_builtin_baseline = false;
        bool config_in_manifest = false;
        auto configuration = paths.get_configuration_and_location();

        vcpkg::Path configuration_file;

        if (configuration.location == ConfigurationLocation::VcpkgConfigurationFile)
        {
            configuration_file = configuration.directory / "vcpkg-configuration.json";
        }

        Json::Object manifest;
        vcpkg::Path manifest_file;
        if (auto p_manifest = paths.get_manifest_and_location().get())
        {
            manifest = p_manifest->manifest;
            manifest_file = p_manifest->path;
            if (manifest.contains("builtin-baseline"))
            {
                has_builtin_baseline = true;
            }
            if (manifest.contains("vcpkg-configuration"))
            {
                // it should be impossible to have both a manifest vcpkg-configuration, and a regular
                // vcpkg-configuration
                Checks::check_exit(VCPKG_LINE_INFO, configuration_file.empty());
                config_in_manifest = true;
            }
        }

        if (configuration_file.empty() && !has_builtin_baseline && !config_in_manifest)
        {
            msg::println(Color::warning, msgUpdateBaselineNoConfiguration);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        if (has_builtin_baseline)
        {
            auto new_baseline = get_baseline_from_git_repo(paths, builtin_git_url);
            if (auto p = new_baseline.get())
            {
                manifest.insert_or_replace("builtin-baseline", std::move(*p));
            }
        }
        else if (auto default_reg = configuration.config.default_reg.get())
        {
            auto new_baseline = get_latest_baseline(paths, *default_reg);
            if (auto p = new_baseline.get())
            {
                default_reg->baseline = std::move(*p);
            }
        }

        for (auto& reg : configuration.config.registries)
        {
            auto new_baseline = get_latest_baseline(paths, reg);
            if (auto p = new_baseline.get())
            {
                reg.baseline = std::move(*p);
            }
        }

        if (config_in_manifest)
        {
            manifest.insert_or_replace("vcpkg-configuration", configuration.config.serialize());
        }
        else
        {
            paths.get_filesystem().write_contents(
                configuration_file, Json::stringify(configuration.config.serialize(), {}), VCPKG_LINE_INFO);
        }

        if (config_in_manifest || has_builtin_baseline)
        {
            paths.get_filesystem().write_contents(manifest_file, Json::stringify(manifest, {}), VCPKG_LINE_INFO);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
