#pragma once

#include <vcpkg/base/fwd/expected.h>

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg::Commands::DependInfo
{
    extern const CommandStructure COMMAND_STRUCTURE;

    enum class DependInfoSortMode
    {
        Lexicographical,
        Topological,
        ReverseTopological
    };

    enum class DependInfoFormat
    {
        List,
        Tree,
        Dot,
        Dgml
    };

    struct DependInfoStrategy
    {
        DependInfoSortMode sort_mode;
        DependInfoFormat format;
        int max_depth;
        bool show_depth;
    };

    ExpectedL<DependInfoStrategy> determine_depend_info_mode(const ParsedArguments& args);

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet);
}
