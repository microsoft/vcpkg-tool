#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>

#include <vcpkg/commands.update-baseline.h>
#include <vcpkg/configuration.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;

    DECLARE_AND_REGISTER_MESSAGE(UpdateBaselineNoConfiguration,
                                 (),
                                 "",
                                 "neither `vcpkg.json` nor `vcpkg-configuration.json` exist to update.");

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

    static void update_baseline_in_config(const VcpkgPaths& paths, RegistryConfig& reg)
    {
        auto url = reg.registry_location();
        auto new_baseline_res = reg.get_latest_baseline(paths);

        if (auto new_baseline = new_baseline_res.get())
        {
            if (*new_baseline != reg.baseline)
            {
                msg::println(msgUpdateBaselineUpdatedBaseline,
                             msg::url = url,
                             msg::old_value = reg.baseline.value_or("(none)"),
                             msg::new_value = new_baseline->value_or("(none)"));
                reg.baseline = std::move(*new_baseline);
            }
            // new_baseline == reg.baseline
            else
            {
                msg::println(msgUpdateBaselineNoUpdate, msg::url = url, msg::value = reg.baseline.value_or("(none)"));
            }

            return;
        }

        // this isn't an error, since we want to continue attempting to update baselines
        msg::print_warning(
            msg::format(msgUpdateBaselineNoUpdate, msg::url = url, msg::value = reg.baseline.value_or("(none)"))
                .appendnl()
                .append(new_baseline_res.error()));
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

        if (has_builtin_baseline || add_builtin_baseline)
        {
            // remove default_reg, since that's filled in with the builtin-baseline
            configuration.config.default_reg = nullopt;

            auto synthesized_registry = RegistryConfig{};
            synthesized_registry.kind = "builtin";
            if (auto p = manifest.manifest.get("builtin-baseline"))
            {
                synthesized_registry.baseline = p->string().to_string();
            }

            update_baseline_in_config(paths, synthesized_registry);

            if (auto p = synthesized_registry.baseline.get())
            {
                manifest.manifest.insert_or_replace("builtin-baseline", std::move(*p));
            }
        }

        if (auto default_reg = configuration.config.default_reg.get())
        {
            update_baseline_in_config(paths, *default_reg);
        }

        for (auto& reg : configuration.config.registries)
        {
            update_baseline_in_config(paths, reg);
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
