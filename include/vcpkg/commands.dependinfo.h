#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::DependInfo
{
    extern const CommandStructure COMMAND_STRUCTURE;

    struct PackageDependInfo
    {
        std::string package;
        int depth;
        std::unordered_set<std::string> features;
        std::vector<std::string> dependencies;
    };

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet);

    void RecurseFindDependencies(int level, std::string currDepend, std::vector<PackageDependInfo> allDepends);

    struct DependInfoCommand : TripletCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet) const override;
    };
}
