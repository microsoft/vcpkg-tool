#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/fmt.h>
#include <vcpkg/base/fwd/optional.h>
#include <vcpkg/base/fwd/stringview.h>
#include <vcpkg/base/fwd/system.h>

#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/path.h>

#include <array>
#include <string>

namespace vcpkg
{
    struct ToolVersion
    {
        std::array<int, 3> cooked; // e.g. 24.8  or 2.7.4 or 1.0.0
        std::string raw;           // e.g. 24.08 or 2.7.4 or 1.0
    };

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

    enum class ToolOs
    {
        Windows,
        Osx,
        Linux,
        FreeBsd,
        OpenBsd,
        Solaris,
    };

    Optional<ToolOs> to_tool_os(StringView os) noexcept;
    StringLiteral to_string_literal(ToolOs os) noexcept;
    LocalizedString all_comma_separated_tool_oses();

    struct ToolDataEntry
    {
        std::string tool;
        ToolOs os;
        Optional<CPUArchitecture> arch;
        ToolVersion version;
        std::string exeRelativePath;
        std::string url;
        std::string sha512;
        std::string archiveName;
    };

    ExpectedL<std::vector<ToolDataEntry>> parse_tool_data(StringView contents, StringView origin);

    const ToolDataEntry* get_raw_tool_data(const std::vector<ToolDataEntry>& tool_data_table,
                                           StringView toolname,
                                           const CPUArchitecture arch,
                                           const ToolOs os);

    struct ToolDataFileDeserializer final : Json::IDeserializer<std::vector<ToolDataEntry>>
    {
        virtual LocalizedString type_name() const override;

        virtual View<StringLiteral> valid_fields() const noexcept override;

        virtual Optional<std::vector<ToolDataEntry>> visit_object(Json::Reader& r,
                                                                  const Json::Object& obj) const override;

        static const ToolDataFileDeserializer instance;
    };

    struct ToolOsDeserializer final : Json::IDeserializer<ToolOs>
    {
        virtual LocalizedString type_name() const override;

        virtual Optional<ToolOs> visit_string(Json::Reader& r, StringView str) const override;

        static const ToolOsDeserializer instance;
    };

    struct ToolVersionDeserializer final : Json::IDeserializer<ToolVersion>
    {
        virtual LocalizedString type_name() const override;

        virtual Optional<ToolVersion> visit_string(Json::Reader& r, StringView str) const override;

        static const ToolVersionDeserializer instance;
    };
}

VCPKG_FORMAT_WITH_TO_STRING_LITERAL_NONMEMBER(vcpkg::ToolOs);
