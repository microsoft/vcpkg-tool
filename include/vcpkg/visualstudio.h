#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/fwd/vcpkgpaths.h>
#include <vcpkg/fwd/visualstudio.h>

#include <string>
#include <vector>

namespace vcpkg::VisualStudio
{
    StringLiteral to_string_literal(ReleaseType release_type) noexcept;

    struct VisualStudioInstance
    {
        Path root_path;
        std::string version;
        ReleaseType release_type;

        VisualStudioInstance(Path&& root_path, std::string&& version, ReleaseType release_type);
        std::string to_string() const;
        std::string major_version() const;
    };

    std::vector<VisualStudioInstance> get_sorted_visual_studio_instances(const ReadOnlyFilesystem& fs);

    ToolsetsInformation find_toolset_instances_preferred_first(
        const ReadOnlyFilesystem& fs, const std::vector<VisualStudioInstance>& sorted_visual_studio_instances);
}

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::VisualStudio::VisualStudioInstance);
VCPKG_FORMAT_WITH_TO_STRING_LITERAL_NONMEMBER(vcpkg::VisualStudio::ReleaseType);
