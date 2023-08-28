#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg
{
    extern const CommandMetadata CommandInitRegistryMetadata;
    void command_init_registry_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs);
}
