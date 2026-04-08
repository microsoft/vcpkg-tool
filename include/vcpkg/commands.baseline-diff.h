#pragma once

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandBaselineDiffMetadata;
    void command_baseline_diff_and_exit(const VcpkgCmdArguments& args,
                                        const VcpkgPaths& paths,
                                        vcpkg::Triplet default_triplet,
                                        vcpkg::Triplet host_triplet);
}
