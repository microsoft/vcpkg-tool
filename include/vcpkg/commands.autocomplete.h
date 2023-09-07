#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandAutocompleteMetadata;
    void command_autocomplete_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
