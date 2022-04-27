#pragma once

#include <vcpkg/base/fwd/expected.h>

#include <vcpkg/fwd/tools.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/stringview.h>

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

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
        static constexpr StringLiteral IFW_BINARYCREATOR = "ifw_binarycreator";
        static constexpr StringLiteral IFW_REPOGEN = "ifw_repogen";
        // This duplicate of 7zip uses msiexec to unpack, which is a fallback for Windows 7.
        static constexpr StringLiteral SEVEN_ZIP_MSI = "7zip_msi";
    }

    struct ToolVersion
    {
        // Has no semantic meaning -- this is only used for pretty user output
        std::string original_text;
        std::vector<uint64_t> version;

        friend bool operator==(const ToolVersion& lhs, const ToolVersion& rhs);
        friend bool operator!=(const ToolVersion& lhs, const ToolVersion& rhs) { return !(lhs == rhs); }
        friend bool operator<(const ToolVersion& lhs, const ToolVersion& rhs);
        friend bool operator>(const ToolVersion& lhs, const ToolVersion& rhs) { return rhs < lhs; }
        friend bool operator>=(const ToolVersion& lhs, const ToolVersion& rhs) { return !(lhs < rhs); }
        friend bool operator<=(const ToolVersion& lhs, const ToolVersion& rhs) { return !(rhs < lhs); }

        static ToolVersion from_values(std::initializer_list<uint64_t>);
        static ExpectedL<ToolVersion> try_parse_git(StringView git_version);
        static ExpectedL<ToolVersion> try_parse_numeric(StringView string_version);
    };

    struct PathAndVersion
    {
        Path path;
        ToolVersion version;
    };

    struct ToolCache
    {
        virtual ~ToolCache() = default;

        virtual const Path& get_tool_path_from_system(const Filesystem& fs, StringView tool) const = 0;
        virtual const Path& get_tool_path(const VcpkgPaths& paths, StringView tool) const = 0;
        virtual const std::string& get_tool_version(const VcpkgPaths& paths, StringView tool) const = 0;
    };

    std::unique_ptr<ToolCache> get_tool_cache(RequireExactVersions abiToolVersionHandling);
}
