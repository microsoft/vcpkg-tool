#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg::Commands
{
    enum class StripMode
    {
        Manual,
        Automatic
    };

    struct StripSetting
    {
        StripMode mode;
        int count;

        // A constructor to enforce the relationship between mode and count
        StripSetting(StripMode mode, int count) : mode(mode), count(count)
        {
            // If mode is Automatic, enforce count to be -1
            if (mode == StripMode::Automatic) count = -1;
        }
    };

    struct ExtractedArchive
    {
        Path temp_path;
        Path base_path;
        std::vector<Path> proximate_to_temp;
    };

    // Returns athe set of move operations required to deploy an archive after applying a strip operation. Each .first
    // should be move to .second. If .second is empty, the file should not be deployed.
    std::vector<std::pair<Path, Path>> get_archive_deploy_operations(const ExtractedArchive& archive,
                                                                     StripSetting strip_setting);
    // Returns the number of leading directories that are common to all paths, excluding their last path element.
    // Pre: There are no duplicate entries in paths.
    // Pre: Every entry in paths is lexically_normal.
    // Both conditions are usually met by calling this function with the result of
    // get_regular_files_recursive_lexically_proximate.
    size_t get_common_directories_count(std::vector<Path> paths);
    void command_extract_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
}
