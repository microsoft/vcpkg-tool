#pragma once

namespace vcpkg
{
    struct CiBaseline;
    struct CiBaselineLine;
    struct TripletExclusions;
    struct ExclusionsMap;
    struct ExclusionPredicate;

    enum class CiBaselineState
    {
        Skip,
        Fail,
    };
    enum class SkipFailures : bool
    {
        No,
        Yes,
    };
}
