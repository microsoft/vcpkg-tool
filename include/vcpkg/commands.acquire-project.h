#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands
{
    struct AcquireProjectCommand : PathsCommand
    {
        void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}
