#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/message_sinks.h>
#include <vcpkg/base/fwd/span.h>
#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/fwd/tools.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/path.h>

namespace vcpkg
{
    enum class ExtractionType
    {
        Unknown,
        Tar,
        Zip,
        SevenZip,
        Nupkg,
        Msi,
        Exe,
        SelfExtracting7z
    };

    // Extract `archive` to `to_path` using `tar_tool`.
    bool extract_tar(DiagnosticContext& context, const Path& tar_tool, const Path& archive, const Path& to_path);
    // Extract `archive` to `to_path` using `cmake_tool`. (CMake's built in tar)
    bool extract_tar_cmake(DiagnosticContext& context,
                           const Path& cmake_tool,
                           const Path& archive,
                           const Path& to_path);
    bool extract_archive(DiagnosticContext& context,
                         const Filesystem& fs,
                         const ToolCache& tools,
                         const Path& archive,
                         const Path& to_path);
    // extract `archive` to a sibling temporary subdirectory of `to_path` and returns that path
    Optional<Path> extract_archive_to_temp_subdirectory(DiagnosticContext& context,
                                                        const Filesystem& fs,
                                                        const ToolCache& tools,
                                                        const Path& archive,
                                                        const Path& to_path);

    ExtractionType guess_extraction_type(const Path& archive);
#ifdef _WIN32
    // Extract the 7z archive part of a self extracting 7z installer
    bool win32_extract_self_extracting_7z(DiagnosticContext& context,
                                          const Filesystem& fs,
                                          const Path& archive,
                                          const Path& to_path);
#endif

    struct ZipTool
    {
        bool setup(DiagnosticContext& context, const Filesystem& fs, const ToolCache& tools);

        // Compress the source directory into the destination file.
        bool compress_directory_to_zip(DiagnosticContext& context,
                                       const Filesystem& fs,
                                       const Path& source,
                                       const Path& destination) const;

        Command decompress_zip_archive_cmd(const Path& dst, const Path& archive_path) const;

    private:
#if defined _WIN32
        Optional<Path> seven_zip;
#endif
    };
}
