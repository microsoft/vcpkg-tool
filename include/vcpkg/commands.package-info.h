#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandPackageInfoMetadata;

    void command_package_info_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
