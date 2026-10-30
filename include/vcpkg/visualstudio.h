#pragma once

#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/files.h>

#include <string>
#include <vector>

namespace vcpkg
{
    struct ToolsetsInformation
    {
        std::vector<Toolset> toolsets;

#if defined(_WIN32)
        std::vector<Path> paths_examined;
        std::vector<Toolset> excluded_toolsets;
        LocalizedString get_localized_debug_info() const;
#endif
    };
}

#if defined(_WIN32)

namespace vcpkg::VisualStudio
{
    std::vector<std::string> get_visual_studio_instances(const ReadOnlyFilesystem& fs);

    ToolsetsInformation find_toolset_instances_preferred_first(const ReadOnlyFilesystem& fs);

    struct VisualStudioInstanceDetails
    {
        Path root_path;
        std::string version;
        std::string release_type;
        // Empty, or a healthy install, unless the instance was found by
        // vswhere.
        std::string display_name;
        std::string display_version;
        bool is_complete = true;
        bool is_reboot_required = false;
        // The toolsets vcpkg can use from this instance, latest first, each
        // full_version once.
        std::vector<Toolset> toolsets;
    };

    // Every instance, preferred first, with what vcpkg finds in it. Nothing is
    // selected: this is what vcpkg would choose from.
    std::vector<VisualStudioInstanceDetails> get_visual_studio_instance_details(const ReadOnlyFilesystem& fs);
}

#endif
