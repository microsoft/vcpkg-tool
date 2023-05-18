#include <vcpkg/base/util.h>

#include <vcpkg-test/mockcmakevarprovider.h>

namespace vcpkg::Test
{
    Optional<const std::unordered_map<std::string, std::string>&> MockCMakeVarProvider::get_generic_triplet_vars(
        Triplet triplet) const
    {
        return Util::maybe_value(generic_triplet_vars, triplet);
    }

    Optional<const std::unordered_map<std::string, std::string>&> MockCMakeVarProvider::get_dep_info_vars(
        const PackageSpec& spec) const
    {
        return Util::maybe_value(dep_info_vars, spec);
    }

    Optional<const std::unordered_map<std::string, std::string>&> MockCMakeVarProvider::get_tag_vars(
        const PackageSpec& spec) const
    {
        return Util::maybe_value(tag_vars, spec);
    }
}
