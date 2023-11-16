#pragma once

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/system.process.h>

#include <vector>

namespace vcpkg
{
    Command make_cmake_cmd(const VcpkgPaths& paths,
                           const Path& cmake_script,
                           std::vector<CMakeVariable>&& pass_variables);
}
