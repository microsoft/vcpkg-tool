#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::X_Download
{
    void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs);

    struct XDownloadCommand : BasicCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const override;
    };
}
