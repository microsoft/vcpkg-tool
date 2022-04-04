#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>

#include <vcpkg/commands.update-baseline.h>
#include <vcpkg/configuration.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    namespace msg = vcpkg::msg;
    DECLARE_AND_REGISTER_MESSAGE(UpdateBaselineNoConfiguration,
                                 (),
                                 "",
                                 "there is no `vcpkg-configuration.json` nor manifest file to update.");

    DECLARE_AND_REGISTER_MESSAGE(UpdateBaselineNoExistingBuiltinBaseline,
                                 (msg::option),
                                 "",
                                 "the manifest file currently does not contain a `builtin-baseline` field; in order to "
                                 "add one, pass the --{option} switch.");
    DECLARE_AND_REGISTER_MESSAGE(
        UpdateBaselineAddBaselineNoManifest,
        (msg::option),
        "",
        "the --{option} switch was passed, but there is no manifest file to add a `builtin-baseline` field to.");
}

namespace vcpkg::Commands
{
    static constexpr StringLiteral OPTION_ADD_INITIAL_BASELINE = "add-initial-baseline";

    static constexpr CommandSwitch switches[] = {
        {OPTION_ADD_INITIAL_BASELINE, "add a `builtin-baseline` to a vcpkg.json that doesn't already have it"},
    };

    static const CommandStructure COMMAND_STRUCTURE{
        create_example_string("x-update-baseline"),
        0,
        0,
        {switches},
    };

    void UpdateBaselineCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        auto options = args.parse_arguments(COMMAND_STRUCTURE);

        auto configuration = paths.get_configuration();
        auto p_manifest = paths.get_manifest().get();

        if (configuration.location == ConfigurationLocation::None && !p_manifest)
        {
            msg::print_warning(msgUpdateBaselineNoConfiguration);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        bool has_builtin_baseline = p_manifest && p_manifest->manifest.contains("builtin-baseline");
        bool add_builtin_baseline = Util::Sets::contains(options.switches, OPTION_ADD_INITIAL_BASELINE);

        if (add_builtin_baseline && !p_manifest)
        {
            Checks::msg_exit_with_error(
                VCPKG_LINE_INFO, msgUpdateBaselineAddBaselineNoManifest, msg::option = OPTION_ADD_INITIAL_BASELINE);
        }
        if (!has_builtin_baseline && !add_builtin_baseline && configuration.location == ConfigurationLocation::None)
        {
            msg::print_warning(msgUpdateBaselineNoExistingBuiltinBaseline, msg::option = OPTION_ADD_INITIAL_BASELINE);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        auto updated_configuration = configuration.config;
        auto updated_manifest = p_manifest ? p_manifest->manifest : Json::Object{};
        LocalizedString error;

        if (has_builtin_baseline || add_builtin_baseline)
        {
            auto synthesized_registry = RegistryConfig{};
            synthesized_registry.kind = "builtin";

            auto result = synthesized_registry.get_latest_baseline(paths, error);

            if (auto p = result.get())
            {
                updated_manifest.insert_or_replace("builtin-baseline", *p);
            }
            else if (!error.empty())
            {
                msg::print_warning(error);
            }
        }

        if (auto default_reg = updated_configuration.default_reg.get())
        {
            auto new_baseline = default_reg->get_latest_baseline(paths, error);
            if (auto p = new_baseline.get())
            {
                default_reg->baseline = std::move(*p);
            }
            else if (!error.empty())
            {
                msg::print_warning(error);
            }
        }

        for (auto& reg : updated_configuration.registries)
        {
            auto new_baseline = reg.get_latest_baseline(paths, error);
            if (auto p = new_baseline.get())
            {
                reg.baseline = std::move(*p);
            }
            else if (!error.empty())
            {
                msg::print_warning(error);
            }
        }

        if (configuration.location == ConfigurationLocation::ManifestFile)
        {
            updated_manifest.insert_or_replace("vcpkg-configuration", updated_configuration.serialize());
        }
        else if (configuration.location == ConfigurationLocation::VcpkgConfigurationFile)
        {
            paths.get_filesystem().write_contents(configuration.directory / "vcpkg-configuration.json",
                                                  Json::stringify(configuration.config.serialize(), {}),
                                                  VCPKG_LINE_INFO);
        }

        if (p_manifest)
        {
            paths.get_filesystem().write_contents(
                p_manifest->path, Json::stringify(updated_manifest, {}), VCPKG_LINE_INFO);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
