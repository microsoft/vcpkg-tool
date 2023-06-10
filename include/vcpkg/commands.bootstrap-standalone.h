#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    void command_bootstrap_standalone_and_exit(const VcpkgCmdArguments& args, Filesystem& fs);
}
