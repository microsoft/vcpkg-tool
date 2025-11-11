#pragma once

#include <vcpkg/base/fwd/diagnostics.h>
#include <vcpkg/base/fwd/downloads.h>
#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/tools.h>

#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg
{
    namespace Tools
    {
        static constexpr StringLiteral SEVEN_ZIP = "7zip";
        static constexpr StringLiteral SEVEN_ZIP_ALT = "7z";
        static constexpr StringLiteral SEVEN_ZIP_R = "7zr";
        static constexpr StringLiteral TAR = "tar";
        static constexpr StringLiteral MAVEN = "mvn";
        static constexpr StringLiteral CMAKE = "cmake";
        static constexpr StringLiteral GIT = "git";
        static constexpr StringLiteral GSUTIL = "gsutil";
        static constexpr StringLiteral AWSCLI = "aws";
        static constexpr StringLiteral AZCLI = "az";
        static constexpr StringLiteral AZCOPY = "azcopy";
        static constexpr StringLiteral COSCLI = "coscli";
        static constexpr StringLiteral MONO = "mono";
        static constexpr StringLiteral NINJA = "ninja";
        static constexpr StringLiteral POWERSHELL_CORE = "powershell-core";
        static constexpr StringLiteral NUGET = "nuget";
        static constexpr StringLiteral NODE = "node";
        // This duplicate of CMake should only be used as a fallback to unpack
        static constexpr StringLiteral CMAKE_SYSTEM = "cmake_system";
        static constexpr StringLiteral PYTHON3 = "python3";
        static constexpr StringLiteral PYTHON3_WITH_VENV = "python3_with_venv";
    }

    struct ToolCache
    {
        virtual ~ToolCache() = default;

        virtual const Path* get_tool_path(DiagnosticContext& context, const Filesystem& fs, StringView tool) const = 0;
        virtual const std::string* get_tool_version(DiagnosticContext& context,
                                                    const Filesystem& fs,
                                                    StringView tool) const = 0;
    };

    void extract_prefixed_nonquote(DiagnosticContext& context,
                                   StringLiteral prefix,
                                   StringLiteral tool_name,
                                   Optional<std::string>& maybe_output,
                                   const Path& exe_path);

    void extract_prefixed_nonwhitespace(DiagnosticContext& context,
                                        StringLiteral prefix,
                                        StringLiteral tool_name,
                                        Optional<std::string>& maybe_output,
                                        const Path& exe_path);

    Optional<Path> find_system_tar(DiagnosticContext& context, const ReadOnlyFilesystem& fs);
    Optional<Path> find_system_cmake(DiagnosticContext& context, const ReadOnlyFilesystem& fs);

    std::unique_ptr<ToolCache> get_tool_cache(const AssetCachingSettings& asset_cache_settings,
                                              Path downloads,
                                              Path config_path,
                                              Path tools,
                                              RequireExactVersions abiToolVersionHandling);
}
