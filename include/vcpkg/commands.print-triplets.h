#pragma once

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::PrintTriplets
{
    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet);

    struct PrintTripletsCommand : TripletCommand
    {
        void perform_and_exit(const VcpkgCmdArguments& args,
                              const VcpkgPaths& paths,
                              Triplet default_triplet,
                              Triplet host_triplet) const override;
    };
}
