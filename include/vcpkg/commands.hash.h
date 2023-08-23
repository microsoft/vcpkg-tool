#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg
{
    extern const CommandMetadata CommandHashMetadata;
    void command_hash_and_exit(const VcpkgCmdArguments& args, const Filesystem& paths);
}
