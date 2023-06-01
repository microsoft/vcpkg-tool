#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/span.h>
#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/tools.h>

namespace vcpkg
{
    // Extract `archive` to `to_path` using `tar_tool`.
    void extract_tar(const Path& tar_tool, const Path& archive, const Path& to_path);
    // Extract `archive` to `to_path` using `cmake_tool`. (CMake's built in tar)
    void extract_tar_cmake(const Path& cmake_tool, const Path& archive, const Path& to_path);
    void extract_archive(
        Filesystem& fs, const ToolCache& tools, MessageSink& status_sink, const Path& archive, const Path& to_path);
     // set `to_path` to `archive` contents.
    void set_directory_to_archive_contents(
        Filesystem& fs, const ToolCache& tools, MessageSink& status_sink, const Path& archive, const Path& to_path);
    std::vector<std::pair<Path, Path>> strip_mapping(Filesystem& fs, const Path& directory, int level);
    Path extract_archive_to_temp_subdirectory(
        Filesystem& fs, const ToolCache& tools, MessageSink& status_sink, const Path& archive, const Path& to_path);
#ifdef _WIN32
    // Extract the 7z archive part of a self extracting 7z installer
    void win32_extract_self_extracting_7z(Filesystem& fs, const Path& archive, const Path& to_path);
    // Extract `archive` to `to_path`, deleting `to_path` first. `archive` must be a zip file.
    // This function will use potentially less performant tools that are reliably available on any machine.
    void win32_extract_bootstrap_zip(
        Filesystem& fs, const ToolCache& tools, MessageSink& status_sink, const Path& archive, const Path& to_path);
#endif

    // Compress the source directory into the destination file.
    ExpectedL<Unit> compress_directory_to_zip(
        Filesystem& fs, const ToolCache& tools, MessageSink& status_sink, const Path& source, const Path& destination);

    Command decompress_zip_archive_cmd(const ToolCache& tools,
                                       MessageSink& status_sink,
                                       const Path& dst,
                                       const Path& archive_path);

    std::vector<ExpectedL<Unit>> decompress_in_parallel(View<Command> jobs);
}
