#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/util.h>

#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/commands.update-baseline.h>
#include <vcpkg/configuration.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/installeddatabase.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/triplet.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    struct ManifestVersionSnapshotEntry
    {
        Version version;
        RequestType request_type;
    };

    using ManifestVersionSnapshot = std::map<std::string, ManifestVersionSnapshotEntry, std::less<>>;

    ManifestVersionSnapshot create_manifest_version_snapshot(const VcpkgPaths& paths,
                                                             const ManifestAndPath& manifest,
                                                             const ConfigurationAndSource& configuration,
                                                             const ParsedArguments& options,
                                                             CMakeVars::CMakeVarProvider& var_provider,
                                                             Triplet default_triplet,
                                                             Triplet host_triplet)
    {
        auto registry_set = configuration.instantiate_registry_set(paths);
        auto manifest_scf = parse_manifest_scf_or_exit(manifest, paths, registry_set->is_default_builtin_registry());
        const auto& manifest_core = *manifest_scf->core_paragraph;
        PackageSpec toplevel{manifest_core.name, default_triplet};

        const auto features = get_manifest_features(options, manifest_core, var_provider, toplevel, host_triplet);
        const auto dependencies = get_manifest_dependencies(*manifest_scf, features);

        const bool add_builtin_ports_directory_as_overlay =
            registry_set->is_default_builtin_registry() && !paths.use_git_default_registry();
        auto extended_overlay_port_directories = paths.overlay_ports;
        if (add_builtin_ports_directory_as_overlay)
        {
            extended_overlay_port_directories.builtin_overlay_port_dir.emplace(paths.builtin_ports_directory());
        }

        auto verprovider = make_versioned_portfile_provider(*registry_set);
        auto baseprovider = make_baseline_provider(*registry_set);
        auto oprovider = make_manifest_provider(
            paths.get_filesystem(), extended_overlay_port_directories, manifest.path, std::move(manifest_scf));
        PackagesDirAssigner packages_dir_assigner{paths.packages()};
        ActionPlan install_plan =
            create_versioned_install_plan(
                *verprovider,
                *baseprovider,
                *oprovider,
                var_provider,
                dependencies,
                manifest_core.overrides,
                toplevel,
                packages_dir_assigner,
                {nullptr, host_triplet, UnsupportedPortAction::Error, UseHeadVersion::No, Editable::No})
                .value_or_exit(VCPKG_LINE_INFO);

        Util::erase_remove_if(install_plan.install_actions,
                              [&toplevel](auto&& action) { return action.spec == toplevel; });

        ManifestVersionSnapshot versions;
        for (const auto& action : install_plan.install_actions)
        {
            versions.emplace(action.spec.name(), ManifestVersionSnapshotEntry{action.version, action.request_type});
        }

        return versions;
    }

    void add_version_snapshot_diff_line(std::vector<std::string>& direct_dependencies,
                                        std::vector<std::string>& transitive_dependencies,
                                        const ManifestVersionSnapshotEntry& entry,
                                        std::string&& line)
    {
        if (entry.request_type == RequestType::USER_REQUESTED)
        {
            direct_dependencies.push_back(std::move(line));
            return;
        }

        transitive_dependencies.push_back(std::move(line));
    }

    bool print_version_snapshot_diff_lines(const msg::MessageT<>& header, std::vector<std::string>&& lines)
    {
        if (lines.empty())
        {
            return false;
        }

        Util::sort_unique_erase(lines);
        msg::print(msg::format(header).append_raw(":\n"));
        for (const auto& line : lines)
        {
            msg::write_unlocalized_text(Color::none, line);
            msg::write_unlocalized_text(Color::none, "\n");
        }

        msg::write_unlocalized_text(Color::none, "\n");
        return true;
    }

    void print_version_snapshot_diff(const ManifestVersionSnapshot& previous, const ManifestVersionSnapshot& current)
    {
        std::vector<std::string> direct_dependencies;
        std::vector<std::string> transitive_dependencies;

        for (const auto& current_entry : current)
        {
            const auto previous_entry = previous.find(current_entry.first);
            if (previous_entry == previous.end())
            {
                add_version_snapshot_diff_line(direct_dependencies,
                                               transitive_dependencies,
                                               current_entry.second,
                                               msg::format(msgUpdateBaselineNewDependencyVersion,
                                                           msg::package_name = current_entry.first,
                                                           msg::version = current_entry.second.version)
                                                   .extract_data());
            }
            else if (previous_entry->second.version != current_entry.second.version)
            {
                add_version_snapshot_diff_line(direct_dependencies,
                                               transitive_dependencies,
                                               current_entry.second,
                                               fmt::format("{}: {} -> {}",
                                                           current_entry.first,
                                                           previous_entry->second.version,
                                                           current_entry.second.version));
            }
        }

        for (const auto& previous_entry : previous)
        {
            if (!Util::Maps::contains(current, previous_entry.first))
            {
                add_version_snapshot_diff_line(direct_dependencies,
                                               transitive_dependencies,
                                               previous_entry.second,
                                               msg::format(msgUpdateBaselineRemovedDependencyVersion,
                                                           msg::package_name = previous_entry.first,
                                                           msg::version = previous_entry.second.version)
                                                   .extract_data());
            }
        }

        const bool has_changes = !direct_dependencies.empty() || !transitive_dependencies.empty();
        if (has_changes)
        {
            msg::println(msgUpdateBaselineVersionUpdates);
            msg::write_unlocalized_text(Color::none, "\n");
        }

        print_version_snapshot_diff_lines(msgDirectDependencies, std::move(direct_dependencies));
        print_version_snapshot_diff_lines(msgTransitiveDependencies, std::move(transitive_dependencies));
        if (!has_changes)
        {
            msg::println(msgPortsNoDiff);
        }
    }

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
        {SwitchQuiet, msgCmdUpdateBaselineOptQuiet},
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

    void command_update_baseline_and_exit(const VcpkgCmdArguments& args,
                                          const VcpkgPaths& paths,
                                          Triplet default_triplet,
                                          Triplet host_triplet)
    {
        auto options = args.parse_arguments(CommandUpdateBaselineMetadata);

        const bool add_builtin_baseline = Util::Sets::contains(options.switches, SwitchAddInitialBaseline);
        const bool dry_run = Util::Sets::contains(options.switches, SwitchDryRun);
        const bool quiet = Util::Sets::contains(options.switches, SwitchQuiet);

        auto configuration = paths.get_configuration();

        const auto* manifest_ptr = paths.get_manifest();
        const bool has_manifest = manifest_ptr != nullptr;
        auto manifest = has_manifest ? *manifest_ptr : ManifestAndPath{};
        const auto old_configuration = configuration;
        const auto old_manifest = manifest;

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

        if (has_manifest && !quiet)
        {
            InstallAndBuildDatabaseLock installed_lock{paths.get_filesystem(),
                                                       paths.installed(),
                                                       paths.buildtrees(),
                                                       paths.packages(),
                                                       args.wait_for_lock,
                                                       args.ignore_lock_failures};
            auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths, installed_lock);
            auto& var_provider = *var_provider_storage;
            const auto previous_snapshot = create_manifest_version_snapshot(
                paths, old_manifest, old_configuration, options, var_provider, default_triplet, host_triplet);
            const auto current_snapshot = create_manifest_version_snapshot(
                paths, manifest, configuration, options, var_provider, default_triplet, host_triplet);
            print_version_snapshot_diff(previous_snapshot, current_snapshot);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
