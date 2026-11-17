#pragma once

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/triplet.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    extern const CommandMetadata CommandPrintUsageMetadata;

    void command_print_usage_and_exit(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet);
}
