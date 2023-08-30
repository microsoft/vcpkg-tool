#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandUseMetadata;
    void command_use_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
