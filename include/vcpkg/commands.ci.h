#pragma once

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandCiMetadata;
    void command_ci_and_exit(const VcpkgCmdArguments& args,
                             const VcpkgPaths& paths,
                             Triplet default_triplet,
                             Triplet host_triplet);
}
