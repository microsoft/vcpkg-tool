#include <vcpkg/base/util.h>

#include <vcpkg-test/mockcmakevarprovider.h>

namespace vcpkg::Test
{
    void MockCMakeVarProvider::load_generic_triplet_vars(Triplet triplet) const
    {
        generic_triplet_vars.emplace(triplet, SMap{});
    }

    void MockCMakeVarProvider::load_dep_info_vars(View<PackageSpec> specs, Triplet) const
    {
        for (auto&& spec : specs)
        {
            dep_info_vars.emplace(spec, SMap{});
        }
    }

    void MockCMakeVarProvider::load_tag_vars(View<FullPackageSpec> specs,
                                             View<Path> port_locations,
                                             Triplet host_triplet) const
    {
        (void)port_locations;
        (void)host_triplet;
        for (auto&& spec : specs)
        {
            tag_vars.emplace(spec.package_spec, SMap{});
        }
    }

    Optional<const std::unordered_map<std::string, std::string>&> MockCMakeVarProvider::get_generic_triplet_vars(
        Triplet triplet) const
    {
        return Util::lookup_value(generic_triplet_vars, triplet);
    }

    Optional<const std::unordered_map<std::string, std::string>&> MockCMakeVarProvider::get_dep_info_vars(
        const PackageSpec& spec) const
    {
        return Util::lookup_value(dep_info_vars, spec);
    }

    Optional<const std::unordered_map<std::string, std::string>&> MockCMakeVarProvider::get_tag_vars(
        const PackageSpec& spec) const
    {
        return Util::lookup_value(tag_vars, spec);
    }
}
