#pragma once

#include <vcpkg/base/fwd/downloads.h>
#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/util.h>

#include <string>

namespace vcpkg
{
    ExpectedL<Path> download_vcpkg_standalone_bundle(const DownloadManager& download_manager,
                                                     const Filesystem& fs,
                                                     const Path& download_root);

    int run_configure_environment_command(const VcpkgPaths& paths, View<std::string> args);

    bool more_than_one_mapped(View<const StringLiteral*> candidates,
                              const std::set<std::string, std::less<>>& switches);

    constexpr StringLiteral OPTION_VERSION = "version";

    extern const View<CommandSwitch> CommonAcquireArtifactSwitches;
    extern const View<CommandSetting> CommonSelectArtifactVersionSettings;

    // Copies the switches and settings, but not multisettings from parsed to appended_to, and checks that the switches
    // that apply to artifacts meet semantic rules like only one operating system being selected.
    void forward_common_artifacts_arguments(std::vector<std::string>& appended_to, const ParsedArguments& parsed);
}
