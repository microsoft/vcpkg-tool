#pragma once

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    extern const CommandMetadata CommandExportMetadata;

    void command_export_and_exit(const VcpkgCmdArguments& args,
                                 const VcpkgPaths& paths,
                                 Triplet default_triplet,
                                 Triplet host_triplet);

    void export_integration_files(const Path& raw_exported_dir_path, const VcpkgPaths& paths);
}
