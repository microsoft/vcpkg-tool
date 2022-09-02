#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::AddVersion
{
    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);

    struct AddVersionCommand : PathsCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}