#pragma once

#include <vcpkg/base/fwd/format.h>

#include <vcpkg/base/fmt.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/pragmas.h>
#include <vcpkg/base/stringview.h>

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
    struct formatter<vcpkg::LineInfo, char>
    {
        constexpr auto parse(format_parse_context& ctx) const -> decltype(ctx.begin())
        {
            return vcpkg::basic_format_parse_impl(ctx);
        }
        template<class FormatContext>
        auto format(const vcpkg::LineInfo& li, FormatContext& ctx) const -> decltype(ctx.out())
        {
            return fmt::format_to(ctx.out(), "{}({})", li.file_name, li.line_number);
        }
    };

    template<>
    struct formatter<vcpkg::StringView, char> : formatter<string_view, char>
    {
        template<class FormatContext>
        auto format(vcpkg::StringView sv, FormatContext& ctx) const -> decltype(ctx.out())
        {
            return formatter<string_view, char>::format(string_view(sv.data(), sv.size()), ctx);
        }
    };

    template<>
    struct formatter<std::error_code, char> : formatter<std::string, char>
    {
        constexpr auto parse(format_parse_context& ctx) const -> decltype(ctx.begin())
        {
            return vcpkg::basic_format_parse_impl(ctx);
        }
        template<class FormatContext>
        auto format(const std::error_code& ec, FormatContext& ctx) const -> decltype(ctx.out())
        {
            return formatter<std::string, char>::format(ec.message(), ctx);
        }
    };
}
