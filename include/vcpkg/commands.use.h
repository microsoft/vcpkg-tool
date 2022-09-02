#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands
{
    struct UseCommand : PathsCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}
