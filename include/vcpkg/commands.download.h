#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg
{
    extern const CommandMetadata CommandDownloadMetadata;
    void command_download_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs);
}
