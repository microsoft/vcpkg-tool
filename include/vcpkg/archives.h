#pragma once

#include <vcpkg/base/fwd/span.h>
#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/files.h>

namespace vcpkg
{
    // Extract `archive` to `to_path` using `tar_tool`.
    void extract_tar(const Path& tar_tool, const Path& archive, const Path& to_path);
    // Extract `archive` to `to_path` using `cmake_tool`. (CMake's built in tar)
    void extract_tar_cmake(const Path& cmake_tool, const Path& archive, const Path& to_path);
    // Extract `archive` to `to_path`, deleting `to_path` first.
    void extract_archive(const VcpkgPaths& paths, const Path& archive, const Path& to_path);

    // Compress the source directory into the destination file.
    int compress_directory_to_zip(const VcpkgPaths& paths, const Path& source, const Path& destination);

    Command decompress_zip_archive_cmd(const VcpkgPaths& paths, const Path& dst, const Path& archive_path);

    std::vector<ExitCodeAndOutput> decompress_in_parallel(View<Command> jobs);
}
