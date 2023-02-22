#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::X_Download
{
    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);

    struct XDownloadCommand : PathsCommand
    {
        void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}
