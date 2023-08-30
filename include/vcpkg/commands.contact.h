#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg
{
    extern const CommandMetadata CommandContactMetadata;
    void command_contact_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs);
}
