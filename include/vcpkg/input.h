#pragma once

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/packagespec.h>

namespace vcpkg
{
    PackageSpec check_and_get_package_spec(std::string&& spec_string,
                                           Triplet default_triplet,
                                           const LocalizedString& example_text,
                                           const VcpkgPaths& paths);

    FullPackageSpec check_and_get_full_package_spec(std::string&& spec_string,
                                                    Triplet default_triplet,
                                                    const LocalizedString& example_text,
                                                    const VcpkgPaths& paths);

    void check_triplet(Triplet t, const VcpkgPaths& paths);
}
