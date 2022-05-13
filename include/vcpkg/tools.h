#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/tools.h>
#include <vcpkg/fwd/vcpkgpaths.h>

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
        // This duplicate of 7zip uses msiexec to unpack, which is a fallback for Windows 7.
        static constexpr StringLiteral SEVEN_ZIP_MSI = "7zip_msi";
    }

    struct ToolCache
    {
        virtual ~ToolCache() = default;

        virtual const Path& get_tool_path(StringView tool) const = 0;
        virtual const std::string& get_tool_version(StringView tool) const = 0;
    };

    ExpectedL<Path> find_system_tar(const Filesystem& fs);

    std::unique_ptr<ToolCache> get_tool_cache(Filesystem& fs,
                                              const DownloadManager& downloader,
                                              Path downloads,
                                              Path xml_config,
                                              Path tools,
                                              RequireExactVersions abiToolVersionHandling);
}
