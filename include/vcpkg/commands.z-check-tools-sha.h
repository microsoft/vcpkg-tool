#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg
{
    extern const CommandMetadata CommandCheckToolsShaMetadata;
    void command_check_tools_sha_and_exit(const VcpkgCmdArguments& args, const Filesystem& paths);
}
