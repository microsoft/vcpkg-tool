#pragma once

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <string>
#include <vector>

namespace vcpkg
{
    extern const CommandMetadata CommandExportMetadata;

    void command_export_and_exit(const VcpkgCmdArguments& args,
                                 const VcpkgPaths& paths,
                                 Triplet default_triplet,
                                 Triplet host_triplet);

    void export_integration_files(const Path& raw_exported_dir_path, const VcpkgPaths& paths);

    // treat the installed/triplet directory as if it were the packages directory by removing the triplet
    // e.g.
    // x64-windows/
    // x64-windows/include/
    // x64-windows/include/CONFLICT-A-HEADER-ONLY-CAPS.h
    // x64-windows/include/CONFLICT-a-header-ONLY-mixed.h
    // x64-windows/include/CONFLICT-a-header-ONLY-mixed2.h
    // x64-windows/include/conflict-a-header-only-lowercase.h
    // x64-windows/share/
    // x64-windows/share/a-conflict/
    // x64-windows/share/a-conflict/copyright
    // x64-windows/share/a-conflict/vcpkg.spdx.json
    // x64-windows/share/a-conflict/vcpkg_abi_info.txt
    //
    // becomes
    //
    // include
    // include/CONFLICT-A-HEADER-ONLY-CAPS.h
    // include/CONFLICT-a-header-ONLY-mixed.h
    // include/CONFLICT-a-header-ONLY-mixed2.h
    // include/conflict-a-header-only-lowercase.h
    // share
    // share/a-conflict
    // share/a-conflict/copyright
    // share/a-conflict/vcpkg.spdx.json
    // share/a-conflict/vcpkg_abi_info.txt
    std::vector<std::string> convert_list_to_proximate_files(std::vector<std::string>&& lines,
                                                             StringView triplet_canonical_name);
}
