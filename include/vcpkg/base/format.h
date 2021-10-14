#pragma once

#include <vcpkg/base/lineinfo.h>

#include <fmt/format.h>

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

template<>
struct fmt::formatter<vcpkg::LineInfo>
{
    constexpr auto parse(fmt::format_parse_context& ctx) -> decltype(ctx.begin())
    {
        return vcpkg::basic_format_parse_impl(ctx);
    }
    template<class FormatContext>
    auto format(const vcpkg::LineInfo& li, FormatContext& ctx) -> decltype(ctx.out())
    {
        return format_to(ctx.out(), "{}({})", li.file_name, li.line_number);
    }
};
