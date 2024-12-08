#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg
{
    extern const CommandMetadata CommandFormatFeatureBaselineMetadata;
    void command_format_feature_baseline_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs);
}
