#pragma once

#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/pragmas.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/stringliteral.h>
#include <vcpkg/base/zstringview.h>

VCPKG_MSVC_WARNING(push)
// notes:
// C6239 is not a useful warning for external code; it is
//   (<non-zero constant> && <expression>) always evaluates to the result of <expression>.
// C6385 is a useful warning, but it's incorrect in this case; it thinks that (on line 1238),
//   const char* top = data::digits[exp / 100];
// accesses outside the bounds of data::digits; however, `exp < 10000 => exp / 100 < 100`,
// and thus the access is safe.
VCPKG_MSVC_WARNING(disable : 6239 6385)
#include <fmt/format.h>
VCPKG_MSVC_WARNING(pop)

namespace vcpkg
{
    constexpr auto basic_format_parse_impl(fmt::format_parse_context& ctx) -> decltype(ctx.begin())
    {
        if (ctx.begin() != ctx.end() && *ctx.begin() != '}')
        {
            throw fmt::format_error("invalid format - must be empty");
        }

        return ctx.begin();
    }
}

namespace fmt
{
    template<>
    struct formatter<vcpkg::LineInfo>
    {
        constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
        {
            return vcpkg::basic_format_parse_impl(ctx);
        }
        template<class FormatContext>
        auto format(const vcpkg::LineInfo& li, FormatContext& ctx) -> decltype(ctx.out())
        {
            return format_to(ctx.out(), "{}({})", li.file_name, li.line_number);
        }
    };

    template<>
    struct formatter<vcpkg::StringView> : formatter<string_view>
    {
        // parse is inherited from formatter<string_view>.
        template<class FormatContext>
        auto format(vcpkg::StringView sv, FormatContext& ctx)
        {
            return formatter<string_view>::format(string_view(sv.data(), sv.size()), ctx);
        }
    };

    template<>
    struct formatter<vcpkg::ZStringView> : formatter<string_view>
    {
        template<class FormatContext>
        auto format(vcpkg::ZStringView s, FormatContext& ctx) -> decltype(ctx.out())
        {
            return formatter<string_view>::format(string_view(s.data(), s.size()), ctx);
        }
    };

    template<>
    struct formatter<vcpkg::StringLiteral> : formatter<string_view>
    {
        template<class FormatContext>
        auto format(vcpkg::StringLiteral s, FormatContext& ctx) -> decltype(ctx.out())
        {
            return formatter<string_view>::format(string_view(s.data(), s.size()), ctx);
        }
    };

}
