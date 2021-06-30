#pragma once

#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.interface.h>
#include <vcpkg/portfileprovider.h>

namespace vcpkg::Commands::SetInstalled
{
    extern const CommandStructure COMMAND_STRUCTURE;
    void perform_and_exit_ex(const VcpkgCmdArguments& args,
                             const VcpkgPaths& paths,
                             const PortFileProvider::PathsPortFileProvider& provider,
                             IBinaryProvider& binary_provider,
                             const CMakeVars::CMakeVarProvider& cmake_vars,
                             Dependencies::ActionPlan action_plan,
                             DryRun dry_run,
                             const Optional<path>& pkgsconfig_path,
                             Triplet host_triplet);
    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet);

    struct SetInstalledCommand : TripletCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet) const override;
    };
}
