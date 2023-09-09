#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg
{
    extern const CommandMetadata CommandBootstrapStandaloneMetadata;
    void command_bootstrap_standalone_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs);
}
