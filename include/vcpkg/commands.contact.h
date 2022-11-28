#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::Contact
{
    extern const CommandStructure COMMAND_STRUCTURE;
    void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs);

    struct ContactCommand : BasicCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const override;
    };
}
