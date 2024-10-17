#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandLicenseReportMetadata;
    void command_license_report_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
