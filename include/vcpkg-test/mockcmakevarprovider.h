#pragma once

#include <vcpkg/cmakevars.h>
#include <vcpkg/dependencies.h>

namespace vcpkg::Test
{
    struct MockCMakeVarProvider final : CMakeVars::CMakeVarProvider
    {
        using SMap = std::unordered_map<std::string, std::string>;
        void load_generic_triplet_vars(Triplet triplet) const override;
        void load_dep_info_vars(View<PackageSpec> specs, Triplet) const override;
        void load_tag_vars(View<FullPackageSpec> specs, View<Path> port_locations, Triplet host_triplet) const override;
        Optional<const SMap&> get_generic_triplet_vars(Triplet triplet) const override;
        Optional<const SMap&> get_dep_info_vars(const PackageSpec& spec) const override;
        Optional<const SMap&> get_tag_vars(const PackageSpec& spec) const override;

        mutable std::unordered_map<PackageSpec, SMap> dep_info_vars;
        mutable std::unordered_map<PackageSpec, SMap> tag_vars;
        mutable std::unordered_map<Triplet, SMap> generic_triplet_vars;
    };
}
