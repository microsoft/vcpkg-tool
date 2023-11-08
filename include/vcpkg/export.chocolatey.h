#pragma once

#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/optional.h>

#include <string>
#include <vector>

namespace vcpkg::Chocolatey
{
    struct Options
    {
        Optional<std::string> maybe_maintainer;
        Optional<std::string> maybe_version_suffix;
    };

    void do_export(const std::vector<ExportPlanAction>& export_plan,
                   const VcpkgPaths& paths,
                   const Options& chocolatey_options);
}
