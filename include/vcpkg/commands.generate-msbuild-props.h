#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandGenerateMsbuildPropsMetadata;
    void command_generate_msbuild_props_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
