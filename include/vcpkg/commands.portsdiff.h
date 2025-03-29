#pragma once

#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/optional.h>

#include <vcpkg/versions.h>

#include <string>
#include <vector>

namespace vcpkg
{
    struct UpdatedPort
    {
        std::string port_name;
        VersionDiff version_diff;
    };

    struct PortsDiff
    {
        std::vector<VersionSpec> added_ports;
        std::vector<UpdatedPort> updated_ports;
        std::vector<std::string> removed_ports;
    };

    Optional<PortsDiff> find_portsdiff(DiagnosticContext& context,
                                       const VcpkgPaths& paths,
                                       StringView git_commit_id_for_previous_snapshot,
                                       StringView git_commit_id_for_current_snapshot);

    extern const CommandMetadata CommandPortsdiffMetadata;
    void command_portsdiff_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
