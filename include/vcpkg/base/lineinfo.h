#pragma once

#include <vcpkg/base/fwd/lineinfo.h>

#include <vcpkg/base/format.h>

namespace vcpkg
{
    struct LineInfo
    {
        int line_number;
        const char* file_name;
    };
}

#define VCPKG_LINE_INFO                                                                                                \
    vcpkg::LineInfo { __LINE__, __FILE__ }

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
