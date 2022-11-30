#if defined(_WIN32)
#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands
{
    struct AppLocalCommand : BasicCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const override;
    };
}
#endif
