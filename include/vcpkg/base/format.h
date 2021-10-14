#pragma once

#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/pragmas.h>

VCPKG_MSVC_WARNING(push)
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

}
