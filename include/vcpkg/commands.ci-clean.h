#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandCiCleanMetadata;
    void command_ci_clean_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
