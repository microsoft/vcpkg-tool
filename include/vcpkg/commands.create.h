#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandCreateMetadata;
    int command_create(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
    void command_create_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
