#pragma once

#include <vcpkg/base/fwd/diagnostics.h>
#include <vcpkg/base/fwd/downloads.h>
#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/optional.h>
#include <vcpkg/base/fwd/stringview.h>
#include <vcpkg/base/fwd/system.h>

#include <vcpkg/fwd/tools.h>

#include <vcpkg/base/cache.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/fmt.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/stringview.h>

#include <array>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace vcpkg
{
    namespace Tools
    {
        static constexpr StringLiteral SEVEN_ZIP = "7zip";
        static constexpr StringLiteral SEVEN_ZIP_ALT = "7z";
        static constexpr StringLiteral SEVEN_ZIP_R = "7zr";
        static constexpr StringLiteral TAR = "tar";
        static constexpr StringLiteral MAVEN = "mvn";
        static constexpr StringLiteral CMAKE = "cmake";
        static constexpr StringLiteral GIT = "git";
        static constexpr StringLiteral GSUTIL = "gsutil";
        static constexpr StringLiteral AWSCLI = "aws";
        static constexpr StringLiteral AZCLI = "az";
        static constexpr StringLiteral AZCOPY = "azcopy";
        static constexpr StringLiteral COSCLI = "coscli";
        static constexpr StringLiteral MONO = "mono";
        static constexpr StringLiteral NINJA = "ninja";
        static constexpr StringLiteral POWERSHELL_CORE = "powershell-core";
        static constexpr StringLiteral NUGET = "nuget";
        static constexpr StringLiteral NODE = "node";
        // This duplicate of CMake should only be used as a fallback to unpack
        static constexpr StringLiteral CMAKE_SYSTEM = "cmake_system";
        static constexpr StringLiteral PYTHON3 = "python3";
        static constexpr StringLiteral PYTHON3_WITH_VENV = "python3_with_venv";
    }

    struct ToolCache
    {
        virtual ~ToolCache() = default;

        virtual const Path* get_tool_path(DiagnosticContext& context, const Filesystem& fs, StringView tool) const = 0;
        virtual const std::string* get_tool_version(DiagnosticContext& context,
                                                    const Filesystem& fs,
                                                    StringView tool) const = 0;
    };

    void extract_prefixed_nonquote(DiagnosticContext& context,
                                   StringLiteral prefix,
                                   StringLiteral tool_name,
                                   Optional<std::string>& maybe_output,
                                   const Path& exe_path);

    void extract_prefixed_nonwhitespace(DiagnosticContext& context,
                                        StringLiteral prefix,
                                        StringLiteral tool_name,
                                        Optional<std::string>& maybe_output,
                                        const Path& exe_path);

    Optional<Path> find_system_tar(DiagnosticContext& context, const ReadOnlyFilesystem& fs);
    Optional<Path> find_system_cmake(DiagnosticContext& context, const ReadOnlyFilesystem& fs);

    std::unique_ptr<ToolCache> get_tool_cache(const AssetCachingSettings& asset_cache_settings,
                                              Path downloads,
                                              Path config_path,
                                              Path tools,
                                              RequireExactVersions abiToolVersionHandling);

    struct ToolVersion
    {
        std::array<int, 3> cooked; // e.g. 24.8  or 2.7.4 or 1.0.0
        std::string raw;           // e.g. 24.08 or 2.7.4 or 1.0
    };

    struct ToolData
    {
        std::string name;

        Optional<ToolVersion> version; // at least one of version or min_version must be set
        Optional<std::array<int, 3>> min_version;
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
        NetBsd,
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
        Optional<ToolVersion> version; // at least one of version or min_version must be set
        Optional<ToolVersion> minVersion;
        std::string exeRelativePath;
        std::string url;
        std::string sha512;
        std::string archiveName;
    };

    Optional<std::vector<ToolDataEntry>> parse_tool_data(DiagnosticContext& context,
                                                         StringView contents,
                                                         StringView origin);

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

    template<class Key, class Value, class Compare = std::less<>>
    struct ContextCache
    {
        template<class KeyIsh,
                 class F,
                 std::enable_if_t<std::is_constructible_v<Key, const KeyIsh&> &&
                                      detail::is_callable<Compare&, const Key&, const KeyIsh&>::value,
                                  int> = 0>
        const Value* get_lazy(DiagnosticContext& context, const KeyIsh& k, F&& f) const
        {
            auto it = m_cache.lower_bound(k);
            // lower_bound returns the first iterator such that it->first is greater than or equal to than k, so k must
            // be less than or equal to it->first. If k is greater than it->first, then it must be non-equal so we
            // have a cache miss.
            if (it == m_cache.end() || m_cache.key_comp()(k, it->first))
            {
                ContextBufferedDiagnosticContext cbdc{context};
                auto maybe_result = f(cbdc);
                if (auto success = maybe_result.get())
                {
                    it = m_cache.emplace_hint(it,
                                              std::piecewise_construct,
                                              std::forward_as_tuple(k),
                                              std::forward_as_tuple(std::move(*success), expected_left_tag));
                }
                else
                {
                    it = m_cache.emplace_hint(it,
                                              std::piecewise_construct,
                                              std::forward_as_tuple(k),
                                              std::forward_as_tuple(std::move(cbdc.lines), expected_right_tag));
                }
            }

            if (auto success = it->second.get())
            {
                return success;
            }

            for (const auto& line : it->second.error())
            {
                context.report(line);
            }

            return nullptr;
        }

    private:
        mutable std::map<Key, ExpectedT<Value, std::vector<DiagnosticLine>>, Compare> m_cache;
    };
}

VCPKG_FORMAT_WITH_TO_STRING_LITERAL_NONMEMBER(vcpkg::ToolOs);
