#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg::Commands
{
    void update_baseline_command_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
