#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    void bootstrap_standalone_command_and_exit(const VcpkgCmdArguments& args, Filesystem& fs);
}
