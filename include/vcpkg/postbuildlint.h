#pragma once

#include <vcpkg/fwd/build.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/files.h>

namespace vcpkg
{
    struct PackageSpec;
}

namespace vcpkg::PostBuildLint
{
    size_t perform_all_checks(const PackageSpec& spec,
                              const VcpkgPaths& paths,
                              const PreBuildInfo& pre_build_info,
                              const BuildInfo& build_info,
                              const Path& port_dir);
}
