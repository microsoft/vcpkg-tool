#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/span.h>

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/optional.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/versions.h>

#include <string>
#include <unordered_set>
#include <vector>

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

    struct DependInfoDependency
    {
        PackageSpec spec;
        // true if the dependency is only declared with "host": true by the depending package
        bool host;
    };

    struct DependInfoPackage
    {
        PackageSpec spec;
        Version version;
        // without "core"
        std::vector<std::string> features;
        int depth;
        // SPDX license expression as in the generated SBOM; nullopt if no license is declared
        Optional<std::string> license;
        Optional<std::string> homepage;
        std::vector<DependInfoDependency> dependencies;
        // false if the package is reachable from the requested packages without traversing a host dependency
        bool host_only = true;
    };

    // Sets DependInfoPackage::host_only for all packages: false if reachable from roots via non-host dependencies.
    void mark_host_only_packages(std::vector<DependInfoPackage>& packages, View<PackageSpec> roots);
    std::string create_json_as_string(const std::vector<DependInfoPackage>& packages);

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
        Mermaid,
        Json
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
