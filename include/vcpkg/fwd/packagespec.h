#pragma once

namespace vcpkg
{
    enum class ImplicitDefault : bool
    {
        NO,
        YES,
    };

    struct PackageSpec;
    struct FeatureSpec;
    struct InternalFeatureSet;
    struct FullPackageSpec;
    struct ParsedQualifiedSpecifier;
}
