#pragma once

#include <vcpkg/fwd/dependencies.h>

#include <string>

namespace vcpkg
{
    std::string make_vcpkg_purl(const InstallPlanAction& action);
}
