#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg
{
    extern const CommandMetadata CommandHostInfoMetadata;
    void command_host_info_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs);
}
