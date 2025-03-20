#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/optional.h>
#include <vcpkg/base/fwd/span.h>

#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/packagespec.h>
#include <vcpkg/fwd/portfileprovider.h>
#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace vcpkg::CMakeVars
{
    using CMakeVars = std::unordered_map<std::string, std::string>;

    struct CMakeVarProvider
    {
        virtual ~CMakeVarProvider() = default;

        virtual Optional<const CMakeVars&> get_generic_triplet_vars(Triplet triplet) const = 0;

        virtual Optional<const CMakeVars&> get_dep_info_vars(const PackageSpec& spec) const = 0;

        const CMakeVars& get_or_load_dep_info_vars(const PackageSpec& spec, Triplet host_triplet) const;

        virtual Optional<const CMakeVars&> get_tag_vars(const PackageSpec& spec) const = 0;

        virtual void load_generic_triplet_vars(Triplet triplet) const = 0;

        virtual void load_dep_info_vars(View<PackageSpec> specs, Triplet host_triplet) const = 0;

        virtual void load_tag_vars(View<FullPackageSpec> specs,
                                   View<Path> port_locations,
                                   Triplet host_triplet) const = 0;

        void load_tag_vars(const ActionPlan& action_plan, Triplet host_triplet) const;
    };

    std::unique_ptr<CMakeVarProvider> make_triplet_cmake_var_provider(const VcpkgPaths& paths);
}
