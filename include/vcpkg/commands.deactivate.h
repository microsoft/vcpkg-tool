#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandDeactivateMetadata;
    void command_deactivate_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
