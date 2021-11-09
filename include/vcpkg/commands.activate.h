#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::Activate
{
    struct ActivateCommand : PathsCommand
    {
        void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}