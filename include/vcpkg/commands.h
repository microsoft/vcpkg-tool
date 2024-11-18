#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/span.h>
#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <string>

namespace vcpkg
{
    using BasicCommandFn = void (*)(const VcpkgCmdArguments& args, const Filesystem& fs);
    using PathsCommandFn = void (*)(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
    using TripletCommandFn = void (*)(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet);

    template<class T>
    struct CommandRegistration
    {
        const CommandMetadata& metadata;
        T function;
    };

    extern const View<CommandRegistration<BasicCommandFn>> basic_commands;
    extern const View<CommandRegistration<PathsCommandFn>> paths_commands;
    extern const View<CommandRegistration<TripletCommandFn>> triplet_commands;

    std::vector<const CommandMetadata*> get_all_commands_metadata();

    std::string get_zero_args_usage();
    void print_full_command_list();
}
