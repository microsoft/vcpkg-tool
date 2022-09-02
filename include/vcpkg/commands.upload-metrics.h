#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::UploadMetrics
{
    extern const CommandStructure COMMAND_STRUCTURE;
    struct UploadMetricsCommand : BasicCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const override = 0;
    };
}
