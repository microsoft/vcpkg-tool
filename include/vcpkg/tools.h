#pragma once

#include <vcpkg/base/fwd/expected.h>

#include <vcpkg/fwd/tools.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/stringliteral.h>

#include <vcpkg/versions.h>

#include <string>
#include <utility>

namespace vcpkg
{
    namespace Tools
    {
        constexpr StringLiteral SEVEN_ZIP = "7zip";
        constexpr StringLiteral SEVEN_ZIP_ALT = "7z";
        constexpr StringLiteral TAR = "tar";
        constexpr StringLiteral MAVEN = "mvn";
        constexpr StringLiteral CMAKE = "cmake";
        constexpr StringLiteral GIT = "git";
        constexpr StringLiteral GSUTIL = "gsutil";
        constexpr StringLiteral AWSCLI = "aws";
        constexpr StringLiteral MONO = "mono";
        constexpr StringLiteral NINJA = "ninja";
        constexpr StringLiteral POWERSHELL_CORE = "powershell-core";
        constexpr StringLiteral NUGET = "nuget";
        constexpr StringLiteral ARIA2 = "aria2";
        constexpr StringLiteral NODE = "node";
        constexpr StringLiteral IFW_INSTALLER_BASE = "ifw_installerbase";
        constexpr StringLiteral IFW_BINARYCREATOR = "ifw_binarycreator";
        constexpr StringLiteral IFW_REPOGEN = "ifw_repogen";
    }

    struct PathAndVersion
    {
        Path path;
        DotVersion version;
    };

    struct ToolCache
    {
        virtual ~ToolCache();

        virtual const ExpectedL<Path>& get_tool_path_from_system(const Filesystem& fs, StringView tool) const = 0;
        virtual const ExpectedL<Path>& get_tool_path(const VcpkgPaths& paths, StringView tool) const = 0;
        virtual const ExpectedL<PathAndVersion>& get_tool_versioned(const VcpkgPaths& paths, StringView tool) const = 0;
    };

    ExpectedL<DotVersion> parse_git_version(StringView git_version);

    std::unique_ptr<ToolCache> get_tool_cache(RequireExactVersions abiToolVersionHandling);
}
