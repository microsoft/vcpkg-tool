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

    DECLARE_AND_REGISTER_MESSAGE(UpdateBaselineUpdatedBaseline,
                                 (msg::url, msg::old_value, msg::new_value),
                                 "example of {old_value}, {new_value} is '5507daa796359fe8d45418e694328e878ac2b82f'",
                                 "updated registry '{url}': baseline {old_value} -> {new_value}");
    DECLARE_AND_REGISTER_MESSAGE(UpdateBaselineAlreadyUpToDate,
                                 (msg::url, msg::value),
                                 "example of {value} is '5507daa796359fe8d45418e694328e878ac2b82f'",
                                 "registry '{url}' already up to date: {value}");
    DECLARE_AND_REGISTER_MESSAGE(UpdateBaselineNoUpdate,
                                 (msg::url, msg::value),
                                 "example of {value} is '5507daa796359fe8d45418e694328e878ac2b82f'",
                                 "registry '{url}' not updated: {value}");
}

namespace vcpkg::Commands
{
    static constexpr StringLiteral OPTION_ADD_INITIAL_BASELINE = "add-initial-baseline";
    static constexpr StringLiteral OPTION_DRY_RUN = "dry-run";

    static constexpr CommandSwitch switches[] = {
        {OPTION_ADD_INITIAL_BASELINE, "add a `builtin-baseline` to a vcpkg.json that doesn't already have it"},
        {OPTION_DRY_RUN, "Print out plan without execution"},
    };

    static const CommandStructure COMMAND_STRUCTURE{
        create_example_string("x-update-baseline"),
        0,
        0,
        {switches},
    };

    static void print_update_message(StringView url,
                                     Optional<StringView> p_old_baseline,
                                     const Optional<std::string>& new_baseline)
    {
        auto old_baseline = p_old_baseline.value_or("none");
        if (auto p = new_baseline.get())
        {
            if (p_old_baseline.has_value() && *p == old_baseline)
            {
                msg::println(msgUpdateBaselineAlreadyUpToDate, msg::url = url, msg::value = old_baseline);
            }
            else
            {
                msg::println(msgUpdateBaselineUpdatedBaseline,
                             msg::url = url,
                             msg::old_value = old_baseline,
                             msg::new_value = *p);
            }
        }
        else
        {
            msg::println(msgUpdateBaselineNoUpdate, msg::url = url, msg::value = old_baseline);
        }
    }

    void UpdateBaselineCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        auto options = args.parse_arguments(COMMAND_STRUCTURE);

        const bool add_builtin_baseline = Util::Sets::contains(options.switches, OPTION_ADD_INITIAL_BASELINE);
        const bool dry_run = Util::Sets::contains(options.switches, OPTION_DRY_RUN);

        auto configuration = paths.get_configuration();
        const bool has_manifest = paths.get_manifest().has_value();
        auto manifest = has_manifest ? *paths.get_manifest().get() : ManifestAndLocation{};

        if (configuration.location == ConfigurationLocation::None && !has_manifest)
        {
            msg::print_warning(msgUpdateBaselineNoConfiguration);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        bool has_builtin_baseline = manifest.manifest.contains("builtin-baseline");

        if (add_builtin_baseline && !has_manifest)
        {
            Checks::msg_exit_with_error(
                VCPKG_LINE_INFO, msgUpdateBaselineAddBaselineNoManifest, msg::option = OPTION_ADD_INITIAL_BASELINE);
        }
        if (!has_builtin_baseline && !add_builtin_baseline && configuration.location == ConfigurationLocation::None)
        {
            msg::print_warning(msgUpdateBaselineNoExistingBuiltinBaseline, msg::option = OPTION_ADD_INITIAL_BASELINE);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        LocalizedString error;

        if (has_builtin_baseline || add_builtin_baseline)
        {
            // remove default_reg, since that's filled in with the builtin-baseline
            configuration.config.default_reg = nullopt;

            auto synthesized_registry = RegistryConfig{};
            synthesized_registry.kind = "builtin";

            auto result = synthesized_registry.get_latest_baseline(paths, error);

            auto p_builtin_baseline = manifest.manifest.get("builtin-baseline");
            auto old_baseline = p_builtin_baseline ? Optional<StringView>(p_builtin_baseline->string()) : nullopt;
            print_update_message(synthesized_registry.registry_location(), old_baseline, result);

            if (auto p = result.get())
            {
                manifest.manifest.insert_or_replace("builtin-baseline", std::move(*p));
            }
            else if (!error.empty())
            {
                msg::print_warning(error);
            }
        }

        if (auto default_reg = configuration.config.default_reg.get())
        {
            auto new_baseline = default_reg->get_latest_baseline(paths, error);

            print_update_message(default_reg->registry_location(), default_reg->baseline, new_baseline);

            if (auto p = new_baseline.get())
            {
                default_reg->baseline = std::move(*p);
            }
            else if (!error.empty())
            {
                msg::print_warning(error);
            }
        }

        for (auto& reg : configuration.config.registries)
        {
            auto new_baseline = reg.get_latest_baseline(paths, error);

            print_update_message(reg.registry_location(), reg.baseline, new_baseline);

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
            manifest.manifest.insert_or_replace("vcpkg-configuration", configuration.config.serialize());
        }

        if (!dry_run && configuration.location == ConfigurationLocation::VcpkgConfigurationFile)
        {
            paths.get_filesystem().write_contents(configuration.directory / "vcpkg-configuration.json",
                                                  Json::stringify(configuration.config.serialize(), {}),
                                                  VCPKG_LINE_INFO);
        }

        if (!dry_run && has_manifest)
        {
            paths.get_filesystem().write_contents(
                manifest.path, Json::stringify(manifest.manifest, {}), VCPKG_LINE_INFO);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
