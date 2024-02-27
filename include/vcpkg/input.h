#pragma once

#include <vcpkg/base/fwd/messages.h>
#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/fwd/packagespec.h>
#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    // Parse a package spec without features; typically used by commands which
    // refer to already installed packages which make features irrelevant.
    //
    // Does not assert that the package spec has a valid triplet. This allows
    // such commands to refer to entities that were installed with an overlay
    // triplet or similar which is no longer active.
    PackageSpec parse_package_spec(StringView spec_string,
                                   Triplet default_triplet,
                                   const LocalizedString& example_text);

    // Same as the above but checks the validity of the triplet.
    PackageSpec check_and_get_package_spec(StringView spec_string,
                                           Triplet default_triplet,
                                           const LocalizedString& example_text,
                                           const TripletDatabase& database);

    // Parse a package spec with features, typically used by commands which will
    // install or modify a port.
    //
    // Asserts that the package spec has a valid triplet.
    FullPackageSpec check_and_get_full_package_spec(StringView spec_string,
                                                    Triplet default_triplet,
                                                    const LocalizedString& example_text,
                                                    const TripletDatabase& database);

    void check_triplet(StringView name, const TripletDatabase& database);
}
