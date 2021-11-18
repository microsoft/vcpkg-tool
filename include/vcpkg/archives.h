#pragma once

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/files.h>

namespace vcpkg
{
    // Extract `archive` to `to_path` using `tar_tool`.
    void extract_tar(const Path& tar_tool, const Path& archive, const Path& to_path);
    // Extract `archive` to `path`, deleting `path` first.
    void extract_archive(const VcpkgPaths& paths, const Path& archive, const Path& to_path);
    // Extract `archive` to a subdirectory of `path`, returning the path to the temporary subdirectory.
    Path extract_archive_to_temp_subdirectory(const VcpkgPaths& paths, const Path& archive, const Path& to_path);
}
