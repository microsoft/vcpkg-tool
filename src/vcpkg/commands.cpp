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
#include <vcpkg/commands.extract.h>
#include <vcpkg/commands.fetch.h>
#include <vcpkg/commands.find.h>
#include <vcpkg/commands.format-manifest.h>
#include <vcpkg/commands.generate-message-map.h>
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
#include <vcpkg/commands.update-baseline.h>
#include <vcpkg/commands.update.h>
#include <vcpkg/commands.upgrade.h>
#include <vcpkg/commands.upload-metrics.h>
#include <vcpkg/commands.use.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/commands.vsinstances.h>
#include <vcpkg/commands.z-applocal.h>
#include <vcpkg/commands.z-ce.h>
#include <vcpkg/commands.z-preregister-telemetry.h>
#include <vcpkg/commands.z-print-config.h>

namespace vcpkg::Commands
{
    static constexpr PackageNameAndFunction<BasicCommandFn> basic_commands_storage[] = {
        {"bootstrap-standalone", bootstrap_standalone_command_and_exit},
        {"contact", Contact::perform_and_exit},
        {"x-download", download_command_and_exit},
        {"x-generate-default-message-map", generate_default_message_map_command_and_exit},
        {"hash", Hash::perform_and_exit},
        {"x-init-registry", InitRegistry::perform_and_exit},
        {"version", Version::perform_and_exit},
#if defined(_WIN32)
        {"x-upload-metrics", UploadMetrics::perform_and_exit},
        {"z-applocal", z_applocal_command_and_exit},
#endif // defined(_WIN32)
        {"z-preregister-telemetry", z_preregister_telemetry_and_exit},
    };

    constexpr Span<const PackageNameAndFunction<BasicCommandFn>> basic_commands = basic_commands_storage;

    static constexpr PackageNameAndFunction<PathsCommandFn> paths_commands_storage[] = {
        {"acquire", acquire_command_and_exit},
        {"acquire-project", acquire_project_command_and_exit},
        {"activate", activate_command_and_exit},
        {"add", add_command_and_exit},
        {"x-add-version", AddVersion::perform_and_exit},
        {"autocomplete", Autocomplete::perform_and_exit},
        {"cache", Cache::perform_and_exit},
        {"x-ci-clean", CIClean::perform_and_exit},
        {"x-ci-verify-versions", CIVerifyVersions::perform_and_exit},
        {"create", Create::perform_and_exit},
        {"deactivate", deactivate_command_and_exit},
        {"edit", Edit::perform_and_exit},
        {"x-extract", extract_command_and_exit},
        {"fetch", Fetch::perform_and_exit},
        {"x-generate-msbuild-props", generate_msbuild_props_command_and_exit},
        {"find", find_command_and_exit},
        {"format-manifest", FormatManifest::perform_and_exit},
        {"/?", Help::perform_and_exit},
        {"help", Help::perform_and_exit},
        {"integrate", Integrate::perform_and_exit},
        {"list", List::perform_and_exit},
        {"new", new_command_and_exit},
        {"owns", Owns::perform_and_exit},
        {"x-package-info", PackageInfo::perform_and_exit},
        {"portsdiff", PortsDiff::perform_and_exit},
        {"x-regenerate", regenerate_command_and_exit},
        {"search", search_command_and_exit},
        {"update", Update::perform_and_exit},
        {"x-update-baseline", update_baseline_command_and_exit},
        {"use", use_command_and_exit},
        {"x-vsinstances", VSInstances::perform_and_exit},
        {"z-ce", z_ce_command_and_exit},
    };

    constexpr Span<const PackageNameAndFunction<PathsCommandFn>> paths_commands = paths_commands_storage;

    static constexpr PackageNameAndFunction<TripletCommandFn> triplet_commands_storage[] = {
        {"build", Build::perform_and_exit},
        {"build-external", BuildExternal::perform_and_exit},
        {"x-check-support", CheckSupport::perform_and_exit},
        {"ci", CI::perform_and_exit},
        {"depend-info", DependInfo::perform_and_exit},
        {"env", Env::perform_and_exit},
        {"export", Export::perform_and_exit},
        {"install", Install::perform_and_exit},
        {"remove", Remove::perform_and_exit},
        {"x-set-installed", SetInstalled::perform_and_exit},
        {"upgrade", Upgrade::perform_and_exit},
        {"z-print-config", ZPrintConfig::perform_and_exit},
    };

    constexpr Span<const PackageNameAndFunction<TripletCommandFn>> triplet_commands = triplet_commands_storage;
}
