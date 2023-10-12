#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandZCEMetadata;
    void command_z_ce_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
