#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands
{
    struct BootstrapReadonlyCommand : BasicCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const override;
    };
}
