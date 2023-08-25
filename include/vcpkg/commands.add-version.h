#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandAddVersionMetadata;
    void command_add_version_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}