#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg::Commands::Hash
{
    void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& paths);
}
