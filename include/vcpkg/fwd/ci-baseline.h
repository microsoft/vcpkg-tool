#pragma once

namespace vcpkg
{
    struct CiBaseline;
    struct CiBaselineLine;
    struct CiFeatureBaseline;
    struct TripletExclusions;
    struct ExclusionsMap;
    struct ExclusionPredicate;

    enum class CiBaselineState
    {
        Skip,
        Fail,
        Pass,
    };
    enum class CiFeatureBaselineState
    {
        Skip,
        Fail,
        Cascade,
        Pass,
    };
    enum class SkipFailures : bool
    {
        No,
        Yes,
    };
}
