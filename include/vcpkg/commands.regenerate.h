#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg
{
    struct RegenerateCommand : PathsCommand
    {
        void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}
