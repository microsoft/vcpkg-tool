#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/json.h>

#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands
{
    ExpectedL<Json::Object> build_prototype_manifest(const std::string* name,
                                                     const std::string* version,
                                                     bool option_application,
                                                     bool option_version_relaxed,
                                                     bool option_version_date,
                                                     bool option_version_string);

    struct NewCommand : PathsCommand
    {
        void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const override;
    };
}
