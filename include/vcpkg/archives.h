#pragma once

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/system.process.h>

namespace vcpkg
{
    // Extract `archive` to `to_path` using `tar_tool`.
    void extract_tar(const Path& tar_tool, const Path& archive, const Path& to_path);
    // Extract `archive` to `to_path`, deleting `to_path` first.
    void extract_archive(const VcpkgPaths& paths, const Path& archive, const Path& to_path);

    Command extract_files_command(const VcpkgPaths& paths,
                                  const Path& zip_file,
                                  View<StringView> files,
                                  const Path& destination_dir);
}
