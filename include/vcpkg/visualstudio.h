#pragma once

#include <vcpkg/vcpkgpaths.h>

namespace vcpkg
{
    struct ToolsetsInformation
    {
        std::vector<Toolset> toolsets;

#if defined(_WIN32)
        std::vector<Path> paths_examined;
        std::vector<Toolset> excluded_toolsets;
        msg::LocalizedString get_localized_debug_info() const;
#endif
    };
}

#if defined(_WIN32)

namespace vcpkg::VisualStudio
{
    std::vector<std::string> get_visual_studio_instances(const VcpkgPaths& paths);

    ToolsetsInformation find_toolset_instances_preferred_first(const VcpkgPaths& paths);
}

#endif
