#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    void command_z_preregister_telemetry_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs);
}
