#pragma once

#include <string>
#include <fmt/format.h>
#include <vcpkg/base/fwd/json.h>
#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/lineinfo.h>

namespace vcpkg
{
#if defined(_WIN32)
    enum class Color : unsigned short
    {
        None = 0,
        Success = 0x0A, // FOREGROUND_GREEN | FOREGROUND_INTENSITY
        Error = 0xC,    // FOREGROUND_RED | FOREGROUND_INTENSITY
        Warning = 0xE,  // FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY
    };
#else
    enum class Color : char
    {
        None = 0,
        Success = '2', // [with 9] bright green
        Error = '1',   // [with 9] bright red
        Warning = '3', // [with 9] bright yellow
    };
#endif
}

namespace vcpkg::msg
{
    namespace detail
    {
        template <class Tag, class Type>
        struct MessageArgument
        {
            const char* name;
            const Type* parameter; // always valid
        };

        template <class... Tags>
        struct MessageCheckFormatArgs
        {
            template <class... Tys>
            static constexpr void check_format_args(const detail::MessageArgument<Tags, Tys>&...) noexcept { }
        };

        template <class... Args>
        MessageCheckFormatArgs<Args...> make_message_check_format_args(const Args&... args);

        ::size_t startup_register_message(StringView name, StringView format_string, StringView comment);

        ::size_t number_of_messages();

        // REQUIRES: index < last_message_index()
        StringView get_format_string(::size_t index);
        // REQUIRES: index < last_message_index()
        StringView get_message_name(::size_t index);
        // REQUIRES: index < last_message_index()
        StringView get_default_format_string(::size_t index);
        // REQUIRES: index < last_message_index()
        StringView get_localization_comment(::size_t index);
    }

    void write_text_to_stdout(Color c, StringView sv);

    // load from "locale_base/${language}.json"
    void threadunsafe_initialize_context(const Filesystem& fs, StringView language, const Path& locale_base);
    // load from the json object
    void threadunsafe_initialize_context(const Json::Object& message_map);
    // initialize without any localized messages (use default messages only)
    void threadunsafe_initialize_context();

    template <class Message, class... Ts>
    std::string format(Message, Ts... args)
    {
        Message::check_format_args(args...);
        auto fmt_string = detail::get_format_string(Message::index);
        return fmt::vformat(
            fmt::string_view(fmt_string.begin(), fmt_string.size()),
            fmt::make_format_args(fmt::arg(args.name, *args.parameter)...)
        );
    }


    template <class Message, class... Ts>
    void print(Message m, Ts... args)
    {
        write_text_to_stdout(Color::None, format(m, args...));
    }
    template <class Message, class... Ts>
    void println(Message m, Ts... args)
    {
        write_text_to_stdout(Color::None, format(m, args...));
        write_text_to_stdout(Color::None, "\n");
    }

    template <class Message, class... Ts>
    void print(Color c, Message m, Ts... args)
    {
        write_text_to_stdout(c, format(m, args...));
    }
    template <class Message, class... Ts>
    void println(Color c, Message m, Ts... args)
    {
        write_text_to_stdout(c, format(m, args...));
        write_text_to_stdout(Color::None, "\n");
    }

// these use `constexpr static` instead of `inline` in order to work with GCC 6;
// they are trivial and empty, and their address does not matter, so this is not a problem
#define DECLARE_MSG_ARG(NAME) \
    constexpr static struct NAME ## _t { \
        template <class T> \
        detail::MessageArgument<NAME ## _t, T> operator=(const T& t) const noexcept \
        { \
            return detail::MessageArgument<NAME ## _t, T>{#NAME, &t}; \
        } \
    } NAME = {}

    DECLARE_MSG_ARG(email);
    DECLARE_MSG_ARG(vcpkg_version);
    DECLARE_MSG_ARG(error);
    DECLARE_MSG_ARG(triplet);
    DECLARE_MSG_ARG(version);
    DECLARE_MSG_ARG(line_info);
    DECLARE_MSG_ARG(file);
    DECLARE_MSG_ARG(port);
    DECLARE_MSG_ARG(option);
#undef DECLARE_MSG_ARG

#define REGISTER_MESSAGE(NAME) \
    const ::size_t NAME ## _msg_t :: index = \
        ::vcpkg::msg::detail::startup_register_message( \
            NAME ## _msg_t::name(), \
            NAME ## _msg_t::default_format_string(), \
            NAME ## _msg_t::localization_comment())

#define DECLARE_SIMPLE_MESSAGE(NAME, COMMENT, DEFAULT_STR) \
    constexpr struct NAME ## _msg_t : ::vcpkg::msg::detail::MessageCheckFormatArgs<> { \
        static StringView name() { \
            return #NAME; \
        }; \
        static StringView localization_comment() { \
            return COMMENT; \
        }; \
        static StringView default_format_string() noexcept { \
            return DEFAULT_STR; \
        } \
        static const ::size_t index; \
    } msg ## NAME = {}

#define DECLARE_AND_REGISTER_SIMPLE_MESSAGE(NAME, COMMENT, DEFAULT_STR) \
    DECLARE_SIMPLE_MESSAGE(NAME, COMMENT, DEFAULT_STR); \
    REGISTER_MESSAGE(NAME)

#define DECLARE_MESSAGE(NAME, COMMENT, DEFAULT_STR, ...) \
    constexpr struct NAME ## _msg_t : decltype(::vcpkg::msg::detail::make_message_check_format_args(__VA_ARGS__)) { \
        static StringView name() { \
            return #NAME; \
        } \
        static StringView localization_comment() { \
            return COMMENT; \
        }; \
        static StringView default_format_string() noexcept { \
            return DEFAULT_STR; \
        } \
        static const ::size_t index; \
    } msg ## NAME = {}
#define DECLARE_AND_REGISTER_MESSAGE(NAME, COMMENT, DEFAULT_STR, ...) \
    DECLARE_MESSAGE(NAME, COMMENT, DEFAULT_STR, __VA_ARGS__); \
    REGISTER_MESSAGE(NAME)
}
