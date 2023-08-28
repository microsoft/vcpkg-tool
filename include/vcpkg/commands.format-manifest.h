#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandFormatManifestMetadata;
    void command_format_manifest_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
