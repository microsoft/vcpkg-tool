#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandCacheMetadata;
    void command_cache_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
