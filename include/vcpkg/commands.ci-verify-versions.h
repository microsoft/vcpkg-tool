#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandCiVerifyVersionsMetadata;
    void command_ci_verify_versions_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}