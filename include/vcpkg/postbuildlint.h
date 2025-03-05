#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/fwd/build.h>
#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/packagespec.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <stddef.h>

namespace vcpkg
{
    size_t perform_post_build_lint_checks(const InstallPlanAction& action,
                                          const VcpkgPaths& paths,
                                          const PreBuildInfo& pre_build_info,
                                          const BuildInfo& build_info,
                                          MessageSink& msg_sink);
}
