#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::Hash
{
    struct HashCommand : BasicCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& paths) const override;
    };
}
