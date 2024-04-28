#pragma once

namespace vcpkg
{
    enum class ImplicitDefault : bool
    {
        No,
        Yes,
    };

    enum class AllowFeatures : bool
    {
        No,
        Yes,
    };

    enum class ParseExplicitTriplet
    {
        Forbid,
        Allow,
        Require,
    };

    enum class AllowPlatformSpec : bool
    {
        No,
        Yes,
    };

    struct PackageSpec;
    struct FeatureSpec;
    struct InternalFeatureSet;
    struct FullPackageSpec;
    struct ParsedQualifiedSpecifier;
}
