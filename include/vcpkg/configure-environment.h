#pragma once

#include <vcpkg/base/fwd/downloads.h>
#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/vcpkgcmdarguments.h>

#include <string>
#include <vector>

namespace vcpkg
{
    Optional<Path> provision_node_component(DiagnosticContext& context,
                                            Path exe_path,
                                            const AssetCachingSettings& asset_cache_settings,
                                            const Filesystem& fs,
                                            const Path& download_root,
                                            StringLiteral script_name,
                                            const Optional<std::string>& script_sha512);

    int run_configure_environment_command(const VcpkgPaths& paths, View<std::string> args);

    constexpr CommandSwitch CommonAcquireArtifactSwitches[] = {
        {SwitchWindows, msgArtifactsSwitchWindows},
        {SwitchOsx, msgArtifactsSwitchOsx},
        {SwitchLinux, msgArtifactsSwitchLinux},
        {SwitchFreeBsd, msgArtifactsSwitchFreebsd},
        {SwitchX86, msgArtifactsSwitchX86},
        {SwitchX64, msgArtifactsSwitchX64},
        {SwitchArm, msgArtifactsSwitchARM},
        {SwitchArm64, msgArtifactsSwitchARM64},
        {SwitchTargetX86, msgArtifactsSwitchTargetX86},
        {SwitchTargetX64, msgArtifactsSwitchTargetX64},
        {SwitchTargetArm, msgArtifactsSwitchTargetARM},
        {SwitchTargetArm64, msgArtifactsSwitchTargetARM64},
        {SwitchForce, msgArtifactsSwitchForce},
        {SwitchAllLanguages, msgArtifactsSwitchAllLanguages},
    };

    constexpr CommandSetting CommonSelectArtifactVersionSettings[] = {
        {SwitchVersion, msgArtifactsOptionVersion},
    };

    // Copies the switches and settings, but not multisettings from parsed to appended_to, and checks that the switches
    // that apply to artifacts meet semantic rules like only one operating system being selected.
    void forward_common_artifacts_arguments(std::vector<std::string>& appended_to, const ParsedArguments& parsed);
}
