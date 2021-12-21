#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands
{
    struct AddCommand : PathsCommand
    {
        void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}
