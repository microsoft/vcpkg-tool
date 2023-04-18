#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::DependInfo
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

    extern const CommandStructure COMMAND_STRUCTURE;

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet);

    struct DependInfoCommand : TripletCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet) const override;
    };
}
