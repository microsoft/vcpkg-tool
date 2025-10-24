#pragma once

namespace vcpkg
{
    struct CiBaseline;
    struct CiBaselineLine;
    struct TripletExclusions;
    struct ExclusionsMap;

    enum class CiBaselineState
    {
        Skip,
        Fail,
        Pass,
    };
    enum class SkipFailures : bool
    {
        No,
        Yes,
    };
}
