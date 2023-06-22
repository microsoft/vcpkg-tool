#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg::Commands
{
    struct ExtractedArchive
    {
        Path base_path;
        std::vector<Path> proximate;
    };

    std::vector<std::pair<Path, Path>> strip_map(const ExtractedArchive& archive, int num_leading_dir);
    void command_extract_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}