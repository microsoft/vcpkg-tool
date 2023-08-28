#include <vcpkg/commands.acquire-project.h>
#include <vcpkg/commands.acquire.h>
#include <vcpkg/commands.activate.h>
#include <vcpkg/commands.add-version.h>
#include <vcpkg/commands.add.h>
#include <vcpkg/commands.autocomplete.h>
#include <vcpkg/commands.bootstrap-standalone.h>
#include <vcpkg/commands.build-external.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.cache.h>
#include <vcpkg/commands.check-support.h>
#include <vcpkg/commands.ci-clean.h>
#include <vcpkg/commands.ci-verify-versions.h>
#include <vcpkg/commands.ci.h>
#include <vcpkg/commands.contact.h>
#include <vcpkg/commands.create.h>
#include <vcpkg/commands.deactivate.h>
#include <vcpkg/commands.dependinfo.h>
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
    static constexpr PackageNameAndFunction<BasicCommandFn> basic_commands_storage[] = {
        {"bootstrap-standalone", command_bootstrap_standalone_and_exit},
        {"contact", command_contact_and_exit},
        {"x-download", command_download_and_exit},
        {"format-feature-baseline", command_format_feature_baseline_and_exit},
        {"hash", command_hash_and_exit},
        {"x-init-registry", command_init_registry_and_exit},
        {"version", command_version_and_exit},
#if defined(_WIN32)
        {"z-upload-metrics", command_z_upload_metrics_and_exit},
        {"z-applocal", command_z_applocal_and_exit},
#endif // defined(_WIN32)
        {"z-generate-default-message-map", command_z_generate_default_message_map_and_exit},
        {"z-preregister-telemetry", command_z_preregister_telemetry_and_exit},
    };

    constexpr View<PackageNameAndFunction<BasicCommandFn>> basic_commands = basic_commands_storage;

    static constexpr PackageNameAndFunction<PathsCommandFn> paths_commands_storage[] = {
        {"acquire", command_acquire_and_exit},
        {"acquire-project", command_acquire_project_and_exit},
        {"activate", command_activate_and_exit},
        {"add", command_add_and_exit},
        {"x-add-version", command_add_version_and_exit},
        {"autocomplete", command_autocomplete_and_exit},
        {"cache", command_cache_and_exit},
        {"x-ci-clean", command_ci_clean_and_exit},
        {"x-ci-verify-versions", command_ci_verify_versions_and_exit},
        {"create", command_create_and_exit},
        {"deactivate", command_deactivate_and_exit},
        {"edit", command_edit_and_exit},
        {"fetch", command_fetch_and_exit},
        {"x-generate-msbuild-props", command_generate_msbuild_props_and_exit},
        {"find", command_find_and_exit},
        {"format-manifest", command_format_manifest_and_exit},
        {"/?", command_help_and_exit},
        {"help", command_help_and_exit},
        {"integrate", command_integrate_and_exit},
        {"list", command_list_and_exit},
        {"new", command_new_and_exit},
        {"owns", command_owns_and_exit},
        {"x-package-info", command_package_info_and_exit},
        {"portsdiff", command_portsdiff_and_exit},
        {"x-regenerate", command_regenerate_and_exit},
        {"search", command_search_and_exit},
        {"update", command_update_and_exit},
        {"x-update-baseline", command_update_baseline_and_exit},
        {"x-update-registry", command_update_registry_and_exit},
        {"use", command_use_and_exit},
        {"x-vsinstances", command_vs_instances_and_exit},
        {"z-ce", command_z_ce_and_exit},
        {"z-extract", command_z_extract_and_exit},
    };

    constexpr View<PackageNameAndFunction<PathsCommandFn>> paths_commands = paths_commands_storage;

    static constexpr PackageNameAndFunction<TripletCommandFn> triplet_commands_storage[] = {
        {"build", command_build_and_exit},
        {"build-external", command_build_external_and_exit},
        {"x-check-support", command_check_support_and_exit},
        {"ci", command_ci_and_exit},
        {"depend-info", command_dependinfo_and_exit},
        {"env", command_env_and_exit},
        {"export", command_export_and_exit},
        {"install", command_install_and_exit},
        {"remove", command_remove_and_exit},
        {"test-features", command_test_features_and_exit},
        {"x-set-installed", command_set_installed_and_exit},
        {"upgrade", command_upgrade_and_exit},
        {"z-print-config", command_z_print_config_and_exit},
    };

    constexpr View<PackageNameAndFunction<TripletCommandFn>> triplet_commands = triplet_commands_storage;
}
