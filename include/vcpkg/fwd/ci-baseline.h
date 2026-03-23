#pragma once

namespace vcpkg
{
    struct CiBaseline;
    struct CiBaselineLine;
    struct TripletSkips;
    struct SkipsMap;

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
