#pragma once

#include <fmt/core.h>

namespace vcpkg
{
    void throw_format_error(const char* msg);

    constexpr auto basic_format_parse_impl(fmt::format_parse_context& ctx) -> decltype(ctx.begin())
    {
        if (ctx.begin() != ctx.end() && *ctx.begin() != '}')
        {
            throw_format_error("invalid format - must be empty");
        }

        return ctx.begin();
    }
}
