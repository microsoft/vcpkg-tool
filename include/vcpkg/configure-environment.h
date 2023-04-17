#pragma once

#include <vcpkg/base/fwd/downloads.h>

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg
{
    ExpectedL<Path> download_vcpkg_standalone_bundle(const DownloadManager& download_manager,
                                                     Filesystem& fs,
                                                     const Path& download_root);

    int run_configure_environment_command(const VcpkgPaths& paths, View<std::string> args);
    int run_configure_environment_command(const VcpkgPaths& paths, StringView arg0, View<std::string> args);
}
