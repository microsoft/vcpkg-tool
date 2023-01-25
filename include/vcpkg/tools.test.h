#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/optional.h>
#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/base/files.h>

#include <array>
#include <string>

namespace vcpkg
{
    struct ToolData
    {
        std::string name;

        std::array<int, 3> version;
        // relative path inside tool_dir_subpath
        Path exe_subpath;
        std::string url;
        // relative path from paths.downloads
        Path download_subpath;
        bool is_archive;
        // relative path from paths.tools
        Path tool_dir_subpath;
        std::string sha512;

        Path exe_path(const Path& tools_base_path) const { return tools_base_path / tool_dir_subpath / exe_subpath; }
    };

    Optional<ToolData> parse_tool_data_from_xml(StringView XML, StringView XML_PATH, StringView tool, StringView os, StringView arch);

    Optional<std::array<int, 3>> parse_tool_version_string(StringView string_version);
}
