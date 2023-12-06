#pragma once

#include <vcpkg/base/fwd/optional.h>
#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <string>

namespace vcpkg
{
    void perform_find_port_and_exit(const VcpkgPaths& paths,
                                    bool full_description,
                                    bool enable_json,
                                    Optional<StringView> filter,
                                    View<std::string> overlay_ports);
    extern const CommandMetadata CommandFindMetadata;
    void command_find_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
