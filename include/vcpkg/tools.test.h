#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/json.h>
#include <vcpkg/base/fwd/optional.h>
#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/base/path.h>

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

    Optional<std::array<int, 3>> parse_tool_version_string(StringView string_version);

    struct ArchToolData
    {
        std::string tool;
        std::string os;
        Optional<vcpkg::CPUArchitecture> arch;
        std::string version;
        std::string exeRelativePath;
        std::string url;
        std::string sha512;
        std::string archiveName;
    };

    ExpectedL<std::vector<ArchToolData>> parse_tool_data(StringView contents, StringView origin);

    const ArchToolData* get_raw_tool_data(const std::vector<ArchToolData>& tool_data_table,
                                          StringView toolname,
                                          const CPUArchitecture arch,
                                          StringView os);

    struct ToolDataDeserializer final : Json::IDeserializer<ArchToolData>
    {
        virtual LocalizedString type_name() const override;

        virtual View<StringView> valid_fields() const override;

        virtual Optional<ArchToolData> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        static const ToolDataDeserializer instance;
    };

    struct ToolDataArrayDeserializer final : Json::ArrayDeserializer<ToolDataDeserializer>
    {
        virtual LocalizedString type_name() const override;

        virtual Optional<std::vector<ArchToolData>> visit_object(Json::Reader& r, const Json::Object&) const override;

        static const ToolDataArrayDeserializer instance;
    };
}
