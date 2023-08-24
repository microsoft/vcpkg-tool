#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandVsInstancesMetadata;
    void command_vs_instances_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
