#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg::Commands
{
    struct ExtractedArchive
    {
        Path temp_path;
        Path base_path;
        std::vector<Path> proximate_to_temp;
    };

    std::vector<std::pair<Path, Path>> strip_map(const ExtractedArchive& archive, int num_leading_dir);
    size_t get_auto_strip_count(std::vector<Path> paths);
    void command_extract_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}