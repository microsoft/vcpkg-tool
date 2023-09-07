#include <vcpkg/commands.acquire-project.h>
#include <vcpkg/commands.acquire.h>
#include <vcpkg/commands.activate.h>
#include <vcpkg/commands.add-version.h>
#include <vcpkg/commands.add.h>
#include <vcpkg/commands.autocomplete.h>
#include <vcpkg/commands.bootstrap-standalone.h>
#include <vcpkg/commands.build-external.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.check-support.h>
#include <vcpkg/commands.ci-clean.h>
#include <vcpkg/commands.ci-verify-versions.h>
#include <vcpkg/commands.ci.h>
#include <vcpkg/commands.contact.h>
#include <vcpkg/commands.create.h>
#include <vcpkg/commands.deactivate.h>
#include <vcpkg/commands.depend-info.h>
#include <vcpkg/commands.download.h>
#include <vcpkg/commands.edit.h>
#include <vcpkg/commands.env.h>
#include <vcpkg/commands.export.h>
#include <vcpkg/commands.fetch.h>
#include <vcpkg/commands.find.h>
#include <vcpkg/commands.format-feature-baselinet.h>
#include <vcpkg/commands.format-manifest.h>
#include <vcpkg/commands.generate-msbuild-props.h>
#include <vcpkg/commands.h>
#include <vcpkg/commands.hash.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/commands.init-registry.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/commands.integrate.h>
#include <vcpkg/commands.list.h>
#include <vcpkg/commands.new.h>
#include <vcpkg/commands.owns.h>
#include <vcpkg/commands.package-info.h>
#include <vcpkg/commands.portsdiff.h>
#include <vcpkg/commands.regenerate.h>
#include <vcpkg/commands.remove.h>
#include <vcpkg/commands.search.h>
#include <vcpkg/commands.set-installed.h>
#include <vcpkg/commands.test-features.h>
#include <vcpkg/commands.update-baseline.h>
#include <vcpkg/commands.update-registry.h>
#include <vcpkg/commands.update.h>
#include <vcpkg/commands.upgrade.h>
#include <vcpkg/commands.use.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/commands.vsinstances.h>
#include <vcpkg/commands.z-applocal.h>
#include <vcpkg/commands.z-ce.h>
#include <vcpkg/commands.z-extract.h>
#include <vcpkg/commands.z-generate-message-map.h>
#include <vcpkg/commands.z-preregister-telemetry.h>
#include <vcpkg/commands.z-print-config.h>
#include <vcpkg/commands.z-upload-metrics.h>

namespace vcpkg
{
    static constexpr CommandRegistration<BasicCommandFn> basic_commands_storage[] = {
        {CommandBootstrapStandaloneMetadata, command_bootstrap_standalone_and_exit},
        {CommandContactMetadata, command_contact_and_exit},
        {CommandDownloadMetadata, command_download_and_exit},
        {CommandFormatFeatureBaselineMetadata, command_format_feature_baseline_and_exit},
        {CommandHashMetadata, command_hash_and_exit},
        {CommandInitRegistryMetadata, command_init_registry_and_exit},
        {CommandVersionMetadata, command_version_and_exit},
#if defined(_WIN32)
        {CommandZUploadMetricsMetadata, command_z_upload_metrics_and_exit},
        {CommandZApplocalMetadata, command_z_applocal_and_exit},
#endif // defined(_WIN32)
        {CommandZGenerateDefaultMessageMapMetadata, command_z_generate_default_message_map_and_exit},
        {CommandZPreregisterTelemetryMetadata, command_z_preregister_telemetry_and_exit},
    };

    constexpr View<CommandRegistration<BasicCommandFn>> basic_commands = basic_commands_storage;

    static constexpr CommandRegistration<PathsCommandFn> paths_commands_storage[] = {
        {CommandAcquireMetadata, command_acquire_and_exit},
        {CommandAcquireProjectMetadata, command_acquire_project_and_exit},
        {CommandActivateMetadata, command_activate_and_exit},
        {CommandAddMetadata, command_add_and_exit},
        {CommandAddVersionMetadata, command_add_version_and_exit},
        {CommandAutocompleteMetadata, command_autocomplete_and_exit},
        {CommandCiCleanMetadata, command_ci_clean_and_exit},
        {CommandCiVerifyVersionsMetadata, command_ci_verify_versions_and_exit},
        {CommandCreateMetadata, command_create_and_exit},
        {CommandDeactivateMetadata, command_deactivate_and_exit},
        {CommandEditMetadata, command_edit_and_exit},
        {CommandFetchMetadata, command_fetch_and_exit},
        {CommandGenerateMsbuildPropsMetadata, command_generate_msbuild_props_and_exit},
        {CommandFindMetadata, command_find_and_exit},
        {CommandFormatManifestMetadata, command_format_manifest_and_exit},
        {CommandHelpMetadata, command_help_and_exit},
        {CommandIntegrateMetadata, command_integrate_and_exit},
        {CommandListMetadata, command_list_and_exit},
        {CommandNewMetadata, command_new_and_exit},
        {CommandOwnsMetadata, command_owns_and_exit},
        {CommandPackageInfoMetadata, command_package_info_and_exit},
        {CommandPortsdiffMetadata, command_portsdiff_and_exit},
        {CommandRegenerateMetadata, command_regenerate_and_exit},
        {CommandSearchMetadata, command_search_and_exit},
        {CommandUpdateMetadata, command_update_and_exit},
        {CommandUpdateBaselineMetadata, command_update_baseline_and_exit},
        {CommandUpdateRegistryMetadata, command_update_registry_and_exit},
        {CommandUseMetadata, command_use_and_exit},
        {CommandVsInstancesMetadata, command_vs_instances_and_exit},
        {CommandZCEMetadata, command_z_ce_and_exit},
        {CommandZExtractMetadata, command_z_extract_and_exit},
    };

    constexpr View<CommandRegistration<PathsCommandFn>> paths_commands = paths_commands_storage;

    static constexpr CommandRegistration<TripletCommandFn> triplet_commands_storage[] = {
        {CommandBuildMetadata, command_build_and_exit},
        {CommandBuildExternalMetadata, command_build_external_and_exit},
        {CommandCheckSupportMetadata, command_check_support_and_exit},
        {CommandCiMetadata, command_ci_and_exit},
        {CommandDependInfoMetadata, command_depend_info_and_exit},
        {CommandEnvMetadata, command_env_and_exit},
        {CommandExportMetadata, command_export_and_exit},
        {CommandInstallMetadata, command_install_and_exit},
        {CommandRemoveMetadata, command_remove_and_exit},
        {CommandTestFeaturesMetadata, command_test_features_and_exit},
        {CommandSetInstalledMetadata, command_set_installed_and_exit},
        {CommandUpgradeMetadata, command_upgrade_and_exit},
        {CommandZPrintConfigMetadata, command_z_print_config_and_exit},
    };

    constexpr View<CommandRegistration<TripletCommandFn>> triplet_commands = triplet_commands_storage;

    std::vector<const CommandMetadata*> get_all_commands_metadata()
    {
        std::vector<const CommandMetadata*> result;
        for (auto&& basic_command : basic_commands_storage)
        {
            result.push_back(&basic_command.metadata);
        }

        for (auto&& paths_command : paths_commands_storage)
        {
            result.push_back(&paths_command.metadata);
        }

        for (auto&& triplet_command : triplet_commands_storage)
        {
            result.push_back(&triplet_command.metadata);
        }

        return result;
    }

    static void format_command_usage_entry(HelpTableFormatter& table, const CommandMetadata& metadata)
    {
        table.format(metadata.name, metadata.synopsis.to_string());
    }

    void print_zero_args_usage()
    {
        HelpTableFormatter table;
        table.example(msg::format(msgVcpkgUsage));
        table.format(msg::format(msgResponseFileCode), msg::format(msgHelpResponseFileCommand));
        table.blank();

        table.header(msg::format(msgPackageInstallationHeader));
        format_command_usage_entry(table, CommandExportMetadata);
        format_command_usage_entry(table, CommandInstallMetadata);
        format_command_usage_entry(table, CommandRemoveMetadata);
        format_command_usage_entry(table, CommandSetInstalledMetadata);
        format_command_usage_entry(table, CommandUpgradeMetadata);
        table.blank();

        table.header(msg::format(msgPackageDiscoveryHeader));
        format_command_usage_entry(table, CommandCheckSupportMetadata);
        format_command_usage_entry(table, CommandDependInfoMetadata);
        format_command_usage_entry(table, CommandListMetadata);
        format_command_usage_entry(table, CommandOwnsMetadata);
        format_command_usage_entry(table, CommandPackageInfoMetadata);
        format_command_usage_entry(table, CommandPortsdiffMetadata);
        format_command_usage_entry(table, CommandSearchMetadata);
        format_command_usage_entry(table, CommandUpdateMetadata);
        table.blank();

        table.header(msg::format(msgPackageManipulationHeader));
        format_command_usage_entry(table, CommandAddMetadata);
        format_command_usage_entry(table, CommandAddVersionMetadata);
        format_command_usage_entry(table, CommandCreateMetadata);
        format_command_usage_entry(table, CommandEditMetadata);
        format_command_usage_entry(table, CommandEnvMetadata);
        format_command_usage_entry(table, CommandFormatManifestMetadata);
        format_command_usage_entry(table, CommandHashMetadata);
        format_command_usage_entry(table, CommandInitRegistryMetadata);
        format_command_usage_entry(table, CommandNewMetadata);
        format_command_usage_entry(table, CommandUpdateBaselineMetadata);
        table.blank();

        table.header(msg::format(msgOtherCommandsHeader));
        format_command_usage_entry(table, CommandCiMetadata);
        format_command_usage_entry(table, CommandCiVerifyVersionsMetadata);
        format_command_usage_entry(table, CommandContactMetadata);
        format_command_usage_entry(table, CommandFetchMetadata);
        format_command_usage_entry(table, CommandIntegrateMetadata);
        table.blank();

        table.header(msg::format(msgForMoreHelp));
        table.format("help topics", msg::format(msgHelpTopicsCommand));
        table.format(msg::format(msgCmdHelpTopic), msg::format(msgHelpTopicCommand));
        table.format("help commands", msg::format(msgCmdHelpCommandsSynopsis));
        table.format(msg::format(msgCmdHelpCommands), msg::format(msgCmdHelpCommandSynopsis));
        table.blank();
        table.example(msg::format(msgHelpExampleCommand));

        msg::println(LocalizedString::from_raw(table.m_str));
    }

    void print_full_command_list()
    {
        HelpTableFormatter table;
        auto all_commands = get_all_commands_metadata();
        Util::sort(all_commands,
                   [](const CommandMetadata* lhs, const CommandMetadata* rhs) { return lhs->name < rhs->name; });
        for (auto command : all_commands)
        {
            format_command_usage_entry(table, *command);
        }

        msg::println(LocalizedString::from_raw(table.m_str));
    }
}
