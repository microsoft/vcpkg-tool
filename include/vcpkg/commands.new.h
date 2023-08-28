#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/json.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    ExpectedL<Json::Object> build_prototype_manifest(const std::string* name,
                                                     const std::string* version,
                                                     bool option_application,
                                                     bool option_version_relaxed,
                                                     bool option_version_date,
                                                     bool option_version_string);

    extern const CommandMetadata CommandNewMetadata;
    void command_new_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
