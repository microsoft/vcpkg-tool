#pragma once

#include <vcpkg/base/fwd/expected.h>

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    struct PackageDependInfo
    {
        std::string package;
        int depth;
        std::unordered_set<std::string> features;
        std::vector<std::string> dependencies;
    };

    std::string create_dot_as_string(const std::vector<PackageDependInfo>& depend_info);
    std::string create_dgml_as_string(const std::vector<PackageDependInfo>& depend_info);
    std::string create_mermaid_as_string(const std::vector<PackageDependInfo>& depend_info);

    extern const CommandMetadata CommandDependInfoMetadata;

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
        Dgml,
        Mermaid
    };

    struct DependInfoStrategy
    {
        DependInfoSortMode sort_mode;
        DependInfoFormat format;
        int max_depth;
        bool show_depth;
    };

    ExpectedL<DependInfoStrategy> determine_depend_info_mode(const ParsedArguments& args);

    void command_depend_info_and_exit(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet);
}
