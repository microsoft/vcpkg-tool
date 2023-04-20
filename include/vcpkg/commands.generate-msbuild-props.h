#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg::Commands
{
    void generate_msbuild_props_command_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
