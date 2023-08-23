#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandEditMetadata;
    void command_edit_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
