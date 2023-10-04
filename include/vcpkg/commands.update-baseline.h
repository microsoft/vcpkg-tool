#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandUpdateBaselineMetadata;
    void command_update_baseline_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
