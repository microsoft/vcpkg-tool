#pragma once

#include <vcpkg/commands.interface.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    extern const CommandStructure SearchCommandStructure;

    struct SearchCommand : PathsCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}
