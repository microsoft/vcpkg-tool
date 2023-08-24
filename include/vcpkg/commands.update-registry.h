#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandUpdateRegistryMetadata;
    void command_update_registry_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
