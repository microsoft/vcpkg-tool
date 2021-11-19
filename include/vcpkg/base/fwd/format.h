#pragma once

namespace fmt
{
    inline namespace v8
    {
        template<typename T, typename Char, typename Enable>
        struct formatter;
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
