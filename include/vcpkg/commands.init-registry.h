#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::InitRegistry
{
    void perform_and_exit(const VcpkgCmdArguments& args, Files::Filesystem& fs);

    struct InitRegistryCommand : BasicCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args, Files::Filesystem& fs) const override;
    };
}
