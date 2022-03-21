#pragma once

#include <vcpkg/base/fwd/format.h>

#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/pragmas.h>
#include <vcpkg/base/stringview.h>

VCPKG_MSVC_WARNING(push)
// note:
// C6239 is not a useful warning for external code; it is
//   (<non-zero constant> && <expression>) always evaluates to the result of <expression>.
VCPKG_MSVC_WARNING(disable : 6239)
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
    template<class Char>
    struct formatter<vcpkg::LineInfo, Char>
    {
        constexpr auto parse(format_parse_context& ctx) const -> decltype(ctx.begin())
        {
            return vcpkg::basic_format_parse_impl(ctx);
        }
        template<class FormatContext>
        auto format(const vcpkg::LineInfo& li, FormatContext& ctx) const -> decltype(ctx.out())
        {
            return format_to(ctx.out(), "{}({})", li.file_name, li.line_number);
        }
    };

    template<class Char>
    struct formatter<vcpkg::StringView, Char> : formatter<string_view, Char>
    {
        template<class FormatContext>
        auto format(vcpkg::StringView sv, FormatContext& ctx) const -> decltype(ctx.out())
        {
            return formatter<string_view, Char>::format(string_view(sv.data(), sv.size()), ctx);
        }
    };
}
