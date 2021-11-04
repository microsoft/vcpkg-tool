#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::Z_Forward
{
    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);

    struct ForwardCommand : PathsCommand
    {
        void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}