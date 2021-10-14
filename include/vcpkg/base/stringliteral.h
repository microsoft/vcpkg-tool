#pragma once

#include <vcpkg/base/zstringview.h>

#include <string>

namespace vcpkg
{
    struct StringLiteral : ZStringView
    {
        template<int N>
        constexpr StringLiteral(const char (&str)[N]) : ZStringView(str)
        {
        }

        operator std::string() const { return std::string(data(), size()); }
    };
}

template<>
struct fmt::formatter<vcpkg::StringLiteral> : fmt::formatter<vcpkg::ZStringView>
{
    template<class FormatContext>
    auto format(const vcpkg::ZStringView& s, FormatContext& ctx) -> decltype(ctx.out())
    {
        return fmt::formatter<vcpkg::StringView>::format(s, ctx);
    }
};
