#pragma once

#include <vcpkg/base/fwd/stringview.h>

namespace vcpkg
{
#if defined(_WIN32)
    enum class Color : unsigned short
    {
        none = 0,
        success = 0x0A, // FOREGROUND_GREEN | FOREGROUND_INTENSITY
        error = 0xC,    // FOREGROUND_RED | FOREGROUND_INTENSITY
        warning = 0xE,  // FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY
    };
#else
    enum class Color : char
    {
        none = 0,
        success = '2', // [with 9] bright green
        error = '1',   // [with 9] bright red
        warning = '3', // [with 9] bright yellow
    };
#endif

    struct LocalizedString;
    struct MessageSink;

#define VCPKG_DECL_MSG_TEMPLATE class... MessageTags, class... MessageTypes
#define VCPKG_DECL_MSG_ARGS                                                                                            \
    ::vcpkg::msg::MessageT<MessageTags...> _message_token,                                                             \
        ::vcpkg::msg::TagArg<::vcpkg::identity_t<MessageTags>, MessageTypes>... _message_args
#define VCPKG_EXPAND_MSG_ARGS _message_token, _message_args...

    template<class T>
    struct identity
    {
        using type = T;
    };
    template<class T>
    using identity_t = typename identity<T>::type;

    namespace msg
    {
        template<class... Tags>
        struct MessageT;

        template<class Tag, class Type>
        struct TagArg;

        template<VCPKG_DECL_MSG_TEMPLATE>
        LocalizedString format(VCPKG_DECL_MSG_ARGS);
    }
}

namespace vcpkg::msg
{
    void write_unlocalized_text_to_stdout(Color c, vcpkg::StringView sv);
    void write_unlocalized_text_to_stderr(Color c, vcpkg::StringView sv);
}
