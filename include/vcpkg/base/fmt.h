#pragma once

#include <vcpkg/base/pragmas.h>

VCPKG_MSVC_WARNING(push)
// note:
// base.h(1737): warning C6294: Ill-defined for-loop. Loop body not executed.
// Arises in fmt/base.h template code only when supplied a nontype template parameter 0.
// base.h(1711): warning C6294: Ill-defined for-loop. Loop body not executed.
// Arises in template code only when supplied a nontype template parameter 0 which makes the loop body empty.
// format.h(1058): warning C6240: (<expression> && <non-zero constant>) always evaluates to the result of <expression>
// Arises from <non-zero constant> being a macro
// format.h(1686): warning C6326: Potential comparison of a constant with another constant.
// Also arises from a macro
VCPKG_MSVC_WARNING(disable : 6240 6294 6326)
#include <fmt/base.h>
#include <fmt/compile.h>
#include <fmt/ranges.h>
VCPKG_MSVC_WARNING(pop)

#define VCPKG_FORMAT_AS(Type, Base)                                                                                    \
    template<typename Char>                                                                                            \
    struct fmt::formatter<Type, Char, void> : fmt::formatter<Base, Char, void>                                         \
    {                                                                                                                  \
        template<typename FormatContext>                                                                               \
        auto format(Type const& val, FormatContext& ctx) const -> decltype(ctx.out())                                  \
        {                                                                                                              \
            return fmt::formatter<Base, Char, void>::format(static_cast<Base>(val), ctx);                              \
        }                                                                                                              \
    }

#define VCPKG_FORMAT_WITH_TO_STRING(Type)                                                                              \
    template<typename Char>                                                                                            \
    struct fmt::formatter<Type, Char, void> : fmt::formatter<std::string, Char, void>                                  \
    {                                                                                                                  \
        template<typename FormatContext>                                                                               \
        auto format(Type const& val, FormatContext& ctx) const -> decltype(ctx.out())                                  \
        {                                                                                                              \
            return fmt::formatter<std::string, Char, void>::format(val.to_string(), ctx);                              \
        }                                                                                                              \
    }

#define VCPKG_FORMAT_WITH_TO_STRING_LITERAL_NONMEMBER(Type)                                                            \
    template<typename Char>                                                                                            \
    struct fmt::formatter<Type, Char, void> : fmt::formatter<vcpkg::StringLiteral, Char, void>                         \
    {                                                                                                                  \
        template<typename FormatContext>                                                                               \
        auto format(Type const& val, FormatContext& ctx) const -> decltype(ctx.out())                                  \
        {                                                                                                              \
            return fmt::formatter<vcpkg::StringLiteral, Char, void>::format(to_string_literal(val), ctx);              \
        }                                                                                                              \
    }

template<class T>
std::string adapt_to_string(const T& val)
{
    std::string result;
    val.to_string(result);
    return result;
}
