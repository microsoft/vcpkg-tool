#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg::Commands::Create
{
    extern const CommandStructure COMMAND_STRUCTURE;
    int perform(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
