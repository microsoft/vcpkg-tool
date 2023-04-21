#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.update-baseline.h>
#include <vcpkg/configuration.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands
{
    static constexpr StringLiteral OPTION_ADD_INITIAL_BASELINE = "add-initial-baseline";
    static constexpr StringLiteral OPTION_DRY_RUN = "dry-run";

    static constexpr CommandSwitch switches[] = {
        {OPTION_ADD_INITIAL_BASELINE, []() { return msg::format(msgCmdUpdateBaselineOptInitial); }},
        {OPTION_DRY_RUN, []() { return msg::format(msgCmdUpdateBaselineOptDryRun); }},
    };

    static const CommandStructure COMMAND_STRUCTURE{
        [] { return create_example_string("x-update-baseline"); },
        0,
        0,
        {switches},
    };

    static void update_baseline_in_config(const VcpkgPaths& paths, RegistryConfig& reg)
    {
        auto url = reg.pretty_location();
        auto new_baseline_res = reg.get_latest_baseline(paths);

        if (auto new_baseline = new_baseline_res.get())
        {
            if (*new_baseline != reg.baseline)
            {
                msg::println(msgUpdateBaselineUpdatedBaseline,
                             msg::url = url,
                             msg::old_value = reg.baseline.value_or(""),
                             msg::new_value = new_baseline->value_or(""));
                reg.baseline = std::move(*new_baseline);
            }
            // new_baseline == reg.baseline
            else
            {
                msg::println(msgUpdateBaselineNoUpdate, msg::url = url, msg::value = reg.baseline.value_or(""));
            }

            return;
        }

        // this isn't an error, since we want to continue attempting to update baselines
        msg::println_warning(
            msg::format(msgUpdateBaselineNoUpdate, msg::url = url, msg::value = reg.baseline.value_or(""))
                .append_raw('\n')
                .append(new_baseline_res.error()));
    }

    void UpdateBaselineCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        auto options = args.parse_arguments(COMMAND_STRUCTURE);

        const bool add_builtin_baseline = Util::Sets::contains(options.switches, OPTION_ADD_INITIAL_BASELINE);
        const bool dry_run = Util::Sets::contains(options.switches, OPTION_DRY_RUN);

        auto configuration = paths.get_configuration();

        const bool has_manifest = paths.get_manifest().has_value();
        auto manifest = has_manifest ? *paths.get_manifest().get() : ManifestAndPath{};

        if (configuration.source == ConfigurationSource::None && !has_manifest)
        {
            msg::println_warning(msgUpdateBaselineNoConfiguration);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        bool has_builtin_baseline = manifest.manifest.contains("builtin-baseline");

        if (add_builtin_baseline && !has_manifest)
        {
            Checks::msg_exit_with_error(
                VCPKG_LINE_INFO, msgUpdateBaselineAddBaselineNoManifest, msg::option = OPTION_ADD_INITIAL_BASELINE);
        }
        if (!has_builtin_baseline && !add_builtin_baseline && configuration.source == ConfigurationSource::None)
        {
            msg::println_warning(msgUpdateBaselineNoExistingBuiltinBaseline, msg::option = OPTION_ADD_INITIAL_BASELINE);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        if (has_builtin_baseline || add_builtin_baseline)
        {
            // remove default_reg, since that's filled in with the builtin-baseline
            configuration.config.default_reg = nullopt;

            RegistryConfig synthesized_registry;
            synthesized_registry.kind = "builtin";
            if (auto p = manifest.manifest.get("builtin-baseline"))
            {
                synthesized_registry.baseline = p->string(VCPKG_LINE_INFO).to_string();
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

        if (configuration.source == ConfigurationSource::ManifestFile)
        {
            manifest.manifest.insert_or_replace("vcpkg-configuration", configuration.config.serialize());
        }

        if (!dry_run && configuration.source == ConfigurationSource::VcpkgConfigurationFile)
        {
            paths.get_filesystem().write_contents(configuration.directory / "vcpkg-configuration.json",
                                                  Json::stringify(configuration.config.serialize()),
                                                  VCPKG_LINE_INFO);
        }

        if (!dry_run && has_manifest)
        {
            paths.get_filesystem().write_contents(manifest.path, Json::stringify(manifest.manifest), VCPKG_LINE_INFO);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
