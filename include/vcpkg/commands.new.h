#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::New
{
    struct NewCommand : PathsCommand
    {
        void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}