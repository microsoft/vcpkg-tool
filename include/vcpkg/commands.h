#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg::Commands
{
    using BasicCommandFn = void (*)(const VcpkgCmdArguments& args, Filesystem& fs);
    using PathsCommandFn = void (*)(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
    using TripletCommandFn = void (*)(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet);

    template<class T>
    struct PackageNameAndFunction
    {
        StringLiteral name;
        T function;
    };

    extern const View<PackageNameAndFunction<BasicCommandFn>> basic_commands;
    extern const View<PackageNameAndFunction<PathsCommandFn>> paths_commands;
    extern const View<PackageNameAndFunction<TripletCommandFn>> triplet_commands;
}
