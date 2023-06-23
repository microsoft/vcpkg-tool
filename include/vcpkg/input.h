#pragma once

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/packagespec.h>

namespace vcpkg
{
    PackageSpec check_and_get_package_spec(StringView spec_string,
                                           Triplet default_triplet,
                                           bool& default_triplet_used,
                                           const LocalizedString& example_text,
                                           const TripletDatabase& database);

    FullPackageSpec check_and_get_full_package_spec(StringView spec_string,
                                                    Triplet default_triplet,
                                                    bool& default_triplet_used,
                                                    const LocalizedString& example_text,
                                                    const TripletDatabase& database);

    void check_triplet(StringView name, const TripletDatabase& database);
}
