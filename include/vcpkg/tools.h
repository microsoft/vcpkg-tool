#pragma once

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/files.h>

#include <string>
#include <utility>

namespace vcpkg
{
    namespace Tools
    {
        static const std::string SEVEN_ZIP = "7zip";
        static const std::string SEVEN_ZIP_ALT = "7z";
        static const std::string TAR = "tar";
        static const std::string MAVEN = "mvn";
        static const std::string CMAKE = "cmake";
        static const std::string GIT = "git";
        static const std::string GSUTIL = "gsutil";
        static const std::string MONO = "mono";
        static const std::string NINJA = "ninja";
        static const std::string POWERSHELL_CORE = "powershell-core";
        static const std::string NUGET = "nuget";
        static const std::string ARIA2 = "aria2";
        static const std::string NODE = "node";
        static const std::string IFW_INSTALLER_BASE = "ifw_installerbase";
        static const std::string IFW_BINARYCREATOR = "ifw_binarycreator";
        static const std::string IFW_REPOGEN = "ifw_repogen";
    }

    enum class RequireExactVersions
    {
        YES,
        NO,
    };

    struct ToolCache
    {
        virtual ~ToolCache() { }

        virtual const Path& get_tool_path_from_system(const Filesystem& fs, const std::string& tool) const = 0;
        virtual const Path& get_tool_path(const VcpkgPaths& paths, const std::string& tool) const = 0;
        virtual const std::string& get_tool_version(const VcpkgPaths& paths, const std::string& tool) const = 0;
    };

    std::unique_ptr<ToolCache> get_tool_cache(RequireExactVersions abiToolVersionHandling);
}
