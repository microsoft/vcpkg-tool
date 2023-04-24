#if defined(_WIN32)
#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    void command_z_applocal_and_exit(const VcpkgCmdArguments& args, Filesystem& fs);
}
#endif
