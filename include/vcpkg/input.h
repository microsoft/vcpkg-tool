#pragma once

#include <vcpkg/packagespec.h>

namespace vcpkg
{
    PackageSpec check_and_get_package_spec(std::string&& spec_string,
                                           Triplet default_triplet,
                                           ZStringView example_text,
                                           const VcpkgPaths& paths);

    FullPackageSpec check_and_get_full_package_spec(std::string&& spec_string,
                                                    Triplet default_triplet,
                                                    ZStringView example_text,
                                                    const VcpkgPaths& paths);

    VersionedPackageSpec check_and_get_versioned_package_spec(std::string&& spec_string, ZStringView example_text);

    void check_triplet(Triplet t, const VcpkgPaths& paths);
}
