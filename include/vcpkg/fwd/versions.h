#pragma once

namespace vcpkg
{
    struct Version;
    struct VersionDiff;
    struct VersionMapLess;
    struct SchemedVersion;
    struct VersionSpec;
    struct VersionSpecHasher;
    struct DotVersion;
    struct DateVersion;
    struct ParsedExternalVersion;

    enum class VerComp
    {
        unk = -2,
        lt = -1, // these values are chosen to align with traditional -1/0/1 for less/equal/greater
        eq = 0,
        gt = 1,
    };

    enum class VersionScheme
    {
        Missing,
        Relaxed,
        Semver,
        Date,
        String
    };

    enum class VersionConstraintKind
    {
        None,
        Minimum
    };
}
