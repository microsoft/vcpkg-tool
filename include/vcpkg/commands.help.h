#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandHelpMetadata;

    void command_help_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);

    void help_topic_valid_triplet(const TripletDatabase& database);
}
