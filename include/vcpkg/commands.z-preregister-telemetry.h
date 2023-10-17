#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

namespace vcpkg
{
    extern const CommandMetadata CommandZPreregisterTelemetryMetadata;
    void command_z_preregister_telemetry_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs);
}
