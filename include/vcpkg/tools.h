#pragma once

#include <vcpkg/base/fwd/downloads.h>
#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/tools.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/stringview.h>

#include <string>
#include <utility>

namespace vcpkg
{
    namespace Tools
    {
        static constexpr StringLiteral SEVEN_ZIP = "7zip";
        static constexpr StringLiteral SEVEN_ZIP_ALT = "7z";
        static constexpr StringLiteral TAR = "tar";
        static constexpr StringLiteral MAVEN = "mvn";
        static constexpr StringLiteral CMAKE = "cmake";
        static constexpr StringLiteral GIT = "git";
        static constexpr StringLiteral GSUTIL = "gsutil";
        static constexpr StringLiteral AWSCLI = "aws";
        static constexpr StringLiteral COSCLI = "coscli";
        static constexpr StringLiteral MONO = "mono";
        static constexpr StringLiteral NINJA = "ninja";
        static constexpr StringLiteral POWERSHELL_CORE = "powershell-core";
        static constexpr StringLiteral NUGET = "nuget";
        static constexpr StringLiteral ARIA2 = "aria2";
        static constexpr StringLiteral NODE = "node";
        static constexpr StringLiteral IFW_INSTALLER_BASE = "ifw_installerbase";
        // This duplicate of CMake should only be used as a fallback to unpack
        static constexpr StringLiteral CMAKE_SYSTEM = "cmake_system";
        // This duplicate of 7zip uses msiexec to unpack, which is a fallback for Windows 7.
        static constexpr StringLiteral SEVEN_ZIP_MSI = "7zip_msi";
        static constexpr StringLiteral PYTHON3 = "python3";
        static constexpr StringLiteral PYTHON3_WITH_VENV = "python3_with_venv";
    }

    struct ToolCache
    {
        virtual ~ToolCache() = default;

        virtual const Path& get_tool_path(StringView tool, MessageSink& status_sink) const = 0;
        virtual const std::string& get_tool_version(StringView tool, MessageSink& status_sink) const = 0;
    };

    ExpectedL<std::string> extract_prefixed_nonwhitespace(StringLiteral prefix,
                                                          StringLiteral tool_name,
                                                          std::string&& output,
                                                          const Path& exe_path);

    ExpectedL<Path> find_system_tar(const ReadOnlyFilesystem& fs);
    ExpectedL<Path> find_system_cmake(const ReadOnlyFilesystem& fs);

    std::unique_ptr<ToolCache> get_tool_cache(const Filesystem& fs,
                                              std::shared_ptr<const DownloadManager> downloader,
                                              Path downloads,
                                              Path xml_config,
                                              Path tools,
                                              RequireExactVersions abiToolVersionHandling);
}
