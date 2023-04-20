#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg::Commands
{
    extern const CommandStructure SearchCommandStructure;

    void search_command_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
