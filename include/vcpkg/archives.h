#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/message_sinks.h>
#include <vcpkg/base/fwd/span.h>
#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/fwd/tools.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/path.h>

namespace vcpkg
{
    enum ExtractionType
    {
        UNKNOWN,
        TAR,
        ZIP,
        NUPKG,
        MSI,
        EXE
    };

    // Extract `archive` to `to_path` using `tar_tool`.
    void extract_tar(const Path& tar_tool, const Path& archive, const Path& to_path);
    // Extract `archive` to `to_path` using `cmake_tool`. (CMake's built in tar)
    void extract_tar_cmake(const Path& cmake_tool, const Path& archive, const Path& to_path);
    void extract_archive(const Filesystem& fs,
                         const ToolCache& tools,
                         MessageSink& status_sink,
                         const Path& archive,
                         const Path& to_path,
                         const ExtractionType& extraction_type = {});
    // set `to_path` to `archive` contents.
    void set_directory_to_archive_contents(const Filesystem& fs,
                                           const ToolCache& tools,
                                           MessageSink& status_sink,
                                           const Path& archive,
                                           const Path& to_path,
                                           const ExtractionType& extraction_type = {});
    Path extract_archive_to_temp_subdirectory(const Filesystem& fs,
                                              const ToolCache& tools,
                                              MessageSink& status_sink,
                                              const Path& archive,
                                              const Path& to_path,
                                              const ExtractionType& extraction_type = {});

#ifdef _WIN32
    // Extract the 7z archive part of a self extracting 7z installer
    void win32_extract_self_extracting_7z(const Filesystem& fs, const Path& archive, const Path& to_path);
    // Extract `archive` to `to_path`, deleting `to_path` first. `archive` must be a zip file.
    // This function will use potentially less performant tools that are reliably available on any machine.
    void win32_extract_bootstrap_zip(const Filesystem& fs,
                                     const ToolCache& tools,
                                     MessageSink& status_sink,
                                     const Path& archive,
                                     const Path& to_path);
#endif

    struct ZipTool
    {
        static ExpectedL<ZipTool> make(const ToolCache& tools, MessageSink& status_sink);

    private:
        ZipTool() = default;

#if defined _WIN32
        Path seven_zip;
#endif

    public:
        // Compress the source directory into the destination file.
        ExpectedL<Unit> compress_directory_to_zip(const Filesystem& fs,
                                                  const Path& source,
                                                  const Path& destination) const;

        Command decompress_zip_archive_cmd(const Path& dst, const Path& archive_path) const;
    };

    std::vector<ExpectedL<Unit>> decompress_in_parallel(View<Command> jobs);
}
