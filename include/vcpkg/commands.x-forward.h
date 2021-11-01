#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::X_Forward
{
    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);

    struct XForwardCommand : PathsCommand
    {
        void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}