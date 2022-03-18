#pragma once

namespace vcpkg
{
    struct CiBaseline;
    struct CiBaselineLine;

    enum class CiBaselineState
    {
        Skip,
        Fail,
    };
}
