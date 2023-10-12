#if defined(_WIN32)
#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg
{
    extern const CommandMetadata CommandZApplocalMetadata;
    void command_z_applocal_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs);
}
#endif
