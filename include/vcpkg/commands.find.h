#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/optional.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg::Commands
{
    void perform_find_port_and_exit(const VcpkgPaths& paths,
                                    bool full_description,
                                    bool enable_json,
                                    Optional<StringView> filter,
                                    View<std::string> overlay_ports);
    void perform_find_artifact_and_exit(const VcpkgPaths& paths, Optional<StringView> filter);
    void find_command_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
