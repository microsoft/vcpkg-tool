#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.update-baseline.h>
#include <vcpkg/configuration.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    void update_baseline_in_config(const VcpkgPaths& paths, RegistryConfig& reg)
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

    constexpr CommandSwitch switches[] = {
        {SwitchAddInitialBaseline, msgCmdUpdateBaselineOptInitial},
        {SwitchDryRun, msgCmdUpdateBaselineOptDryRun},
    };

} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandUpdateBaselineMetadata{
        "x-update-baseline",
        msgCmdUpdateBaselineSynopsis,
        {"vcpkg x-update-baseline"},
        "https://learn.microsoft.com/vcpkg/commands/update-baseline",
        AutocompletePriority::Public,
        0,
        0,
        {switches},
        nullptr,
    };

    void command_update_baseline_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto options = args.parse_arguments(CommandUpdateBaselineMetadata);

        const bool add_builtin_baseline = Util::Sets::contains(options.switches, SwitchAddInitialBaseline);
        const bool dry_run = Util::Sets::contains(options.switches, SwitchDryRun);

        auto configuration = paths.get_configuration();

        const bool has_manifest = paths.get_manifest().has_value();
        auto manifest = has_manifest ? *paths.get_manifest().get() : ManifestAndPath{};

        if (configuration.source == ConfigurationSource::None && !has_manifest)
        {
            msg::println_warning(msgUpdateBaselineNoConfiguration);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        bool has_builtin_baseline = manifest.manifest.contains(JsonIdBuiltinBaseline);

        if (add_builtin_baseline && !has_manifest)
        {
            Checks::msg_exit_with_error(
                VCPKG_LINE_INFO, msgUpdateBaselineAddBaselineNoManifest, msg::option = SwitchAddInitialBaseline);
        }
        if (!has_builtin_baseline && !add_builtin_baseline && configuration.source == ConfigurationSource::None)
        {
            msg::println_warning(msgUpdateBaselineNoExistingBuiltinBaseline, msg::option = SwitchAddInitialBaseline);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        if (has_builtin_baseline || add_builtin_baseline)
        {
            // remove default_reg, since that's filled in with the builtin-baseline
            configuration.config.default_reg.clear();

            RegistryConfig synthesized_registry;
            synthesized_registry.kind = JsonIdBuiltin.to_string();
            if (auto p = manifest.manifest.get(JsonIdBuiltinBaseline))
            {
                synthesized_registry.baseline = p->string(VCPKG_LINE_INFO).to_string();
            }

            update_baseline_in_config(paths, synthesized_registry);

            if (auto p = synthesized_registry.baseline.get())
            {
                manifest.manifest.insert_or_replace(JsonIdBuiltinBaseline, std::move(*p));
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

        switch (configuration.source)
        {
            case ConfigurationSource::None:
                // nothing to do
                break;
            case ConfigurationSource::ManifestFileVcpkgConfiguration:
                manifest.manifest.insert_or_replace(JsonIdVcpkgConfiguration, configuration.config.serialize());
                break;
            case ConfigurationSource::ManifestFileConfiguration:
                manifest.manifest.insert_or_replace(JsonIdConfiguration, configuration.config.serialize());
                break;
            case ConfigurationSource::VcpkgConfigurationFile:
                if (!dry_run)
                {
                    paths.get_filesystem().write_contents(configuration.directory / FileVcpkgConfigurationDotJson,
                                                          Json::stringify(configuration.config.serialize()),
                                                          VCPKG_LINE_INFO);
                }
                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        if (!dry_run && has_manifest)
        {
            paths.get_filesystem().write_contents(manifest.path, Json::stringify(manifest.manifest), VCPKG_LINE_INFO);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
