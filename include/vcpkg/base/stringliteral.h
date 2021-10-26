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

namespace fmt
{
    template<>
    struct formatter<vcpkg::StringLiteral> : formatter<vcpkg::ZStringView>
    {
        template<class FormatContext>
        auto format(const vcpkg::StringLiteral& s, FormatContext& ctx) -> decltype(ctx.out())
        {
            return formatter<vcpkg::StringView>::format(s, ctx);
        }
    };

}
