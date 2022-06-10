#pragma once

#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/portfileprovider.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/optional.h>
#include <vcpkg/base/span.h>

#include <vcpkg/packagespec.h>

namespace vcpkg::CMakeVars
{
    struct CMakeVarProvider
    {
        virtual ~CMakeVarProvider() = default;

        virtual Optional<const std::unordered_map<std::string, std::string>&> get_generic_triplet_vars(
            Triplet triplet) const = 0;

        virtual Optional<const std::unordered_map<std::string, std::string>&> get_dep_info_vars(
            const PackageSpec& spec) const = 0;

        const std::unordered_map<std::string, std::string>& get_or_load_dep_info_vars(const PackageSpec& spec,
                                                                                      Triplet host_triplet) const;

        virtual Optional<const std::unordered_map<std::string, std::string>&> get_tag_vars(
            const PackageSpec& spec) const = 0;

        virtual void load_generic_triplet_vars(Triplet triplet) const = 0;

        virtual void load_dep_info_vars(Span<const PackageSpec> specs, Triplet host_triplet) const = 0;

        virtual void load_tag_vars(Span<const FullPackageSpec> specs,
                                   const PortFileProvider& port_provider,
                                   Triplet host_triplet) const = 0;

        void load_tag_vars(const ActionPlan& action_plan,
                           const PortFileProvider& port_provider,
                           Triplet host_triplet) const;
    };

    std::unique_ptr<CMakeVarProvider> make_triplet_cmake_var_provider(const VcpkgPaths& paths);
}
