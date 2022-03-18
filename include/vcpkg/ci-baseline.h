#pragma once
#include <vcpkg/fwd/ci-baseline.h>

#include <vcpkg/base/expected.h>

#include <string>

namespace vcpkg
{
    struct CiBaselineLine
    {
        std::string port_name;
        std::string triplet_name;
        CiBaselineState state;
    };
}