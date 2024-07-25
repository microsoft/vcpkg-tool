#pragma once

namespace fmt
{
    inline namespace v11
    {
        template<typename T, typename Char, typename Enable>
        struct formatter;

        template<typename Char>
        class basic_string_view;
    }
}

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
