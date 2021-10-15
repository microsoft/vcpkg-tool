#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/json.h>

#include <vcpkg/base/format.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/stringliteral.h>

#include <string>

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
}

namespace vcpkg::msg
{
    struct LocalizedString;

    namespace detail
    {
        template<class Tag, class Type>
        struct MessageArgument
        {
            const Type* parameter; // always valid
        };

        template<class... Tags>
        struct MessageCheckFormatArgs
        {
            static constexpr void check_format_args(const Tags&...) noexcept { }
        };

        LocalizedString internal_vformat(::size_t index, fmt::format_args args);

        template<class... Args>
        MessageCheckFormatArgs<Args...> make_message_check_format_args(const Args&... args);

        ::size_t startup_register_message(StringLiteral name, StringLiteral format_string, StringLiteral comment);

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

    // load from "locale_base/${language}.json"
    void threadunsafe_initialize_context(const Filesystem& fs, StringView language, const Path& locale_base);
    // load from the json object
    void threadunsafe_initialize_context(const Json::Object& message_map);
    // initialize without any localized messages (use default messages only)
    void threadunsafe_initialize_context();

    struct LocalizedString
    {
        LocalizedString() = default;
        operator StringView() const { return m_data; }
        const std::string& data() const { return m_data; }

        static LocalizedString from_string_unchecked(std::string&& s)
        {
            LocalizedString res;
            res.m_data = std::move(s);
            return res;
        }

        LocalizedString& append(const LocalizedString& s);

    private:
        std::string m_data;
        // to avoid lock-in on LocalizedString, these are free functions
        // this allows us to convert to `std::string` in the future without changing All The Code
        friend LocalizedString& append_newline(LocalizedString&);
        friend LocalizedString&& append_newline(LocalizedString&& self) { return std::move(append_newline(self)); }
    };

    inline LocalizedString localized_from_error_code(const std::error_code& ec)
    {
        return LocalizedString::from_string_unchecked(ec.message());
    }

    void write_unlocalized_text_to_stdout(Color c, StringView sv);

    template<class Message, class... Tags, class... Ts>
    LocalizedString format(Message, detail::MessageArgument<Tags, Ts>... args)
    {
        // avoid generating code, but still typeck
        // (and avoid unused typedef warnings)
        static_assert((Message::check_format_args((Tags{})...), true), "");
        return detail::internal_vformat(Message::index,
                                        fmt::make_format_args(fmt::arg(Tags::name(), *args.parameter)...));
    }

    inline void println() { write_unlocalized_text_to_stdout(Color::none, "\n"); }

    inline void print(Color c, const LocalizedString& s) { write_unlocalized_text_to_stdout(c, s); }
    inline void print(const LocalizedString& s) { write_unlocalized_text_to_stdout(Color::none, s); }

    template<class Message, class... Ts>
    void print(Message m, Ts... args)
    {
        print(format(m, args...));
    }
    template<class Message, class... Ts>
    void println(Message m, Ts... args)
    {
        print(append_newline(format(m, args...)));
    }

    template<class Message, class... Ts>
    void print(Color c, Message m, Ts... args)
    {
        print(c, format(m, args...));
    }
    template<class Message, class... Ts>
    void println(Color c, Message m, Ts... args)
    {
        print(c, append_newline(format(m, args...)));
    }

// these use `constexpr static` instead of `inline` in order to work with GCC 6;
// they are trivial and empty, and their address does not matter, so this is not a problem
#define DECLARE_MSG_ARG(NAME)                                                                                          \
    constexpr static struct NAME##_t                                                                                   \
    {                                                                                                                  \
        constexpr static const char* name() { return #NAME; }                                                          \
        template<class T>                                                                                              \
        detail::MessageArgument<NAME##_t, T> operator=(const T& t) const noexcept                                      \
        {                                                                                                              \
            return detail::MessageArgument<NAME##_t, T>{&t};                                                           \
        }                                                                                                              \
    } NAME = {}

    DECLARE_MSG_ARG(email);
    DECLARE_MSG_ARG(error);
    DECLARE_MSG_ARG(version);
    DECLARE_MSG_ARG(value);
    DECLARE_MSG_ARG(pretty_value);
#undef DECLARE_MSG_ARG

// These are `...` instead of
#define DECLARE_MESSAGE(NAME, ARGS, COMMENT, ...)                                                                      \
    constexpr struct NAME##_msg_t : decltype(::vcpkg::msg::detail::make_message_check_format_args ARGS)                \
    {                                                                                                                  \
        static ::vcpkg::StringLiteral name() { return #NAME; }                                                         \
        static ::vcpkg::StringLiteral localization_comment() { return COMMENT; };                                      \
        static ::vcpkg::StringLiteral default_format_string() noexcept { return __VA_ARGS__; }                         \
        static const ::size_t index;                                                                                   \
    } msg##NAME = {}
#define REGISTER_MESSAGE(NAME)                                                                                         \
    const ::size_t NAME##_msg_t ::index = ::vcpkg::msg::detail::startup_register_message(                              \
        NAME##_msg_t::name(), NAME##_msg_t::default_format_string(), NAME##_msg_t::localization_comment())
#define DECLARE_AND_REGISTER_MESSAGE(NAME, ARGS, COMMENT, ...)                                                         \
    DECLARE_MESSAGE(NAME, ARGS, COMMENT, __VA_ARGS__);                                                                 \
    REGISTER_MESSAGE(NAME)
}

namespace fmt
{
    template<>
    struct formatter<vcpkg::msg::LocalizedString> : formatter<vcpkg::StringView>
    {
        // parse is inherited from formatter<StringView>
        template<class FormatContext>
        auto format(const vcpkg::msg::LocalizedString& s, FormatContext& ctx)
        {
            return formatter<vcpkg::StringView>::format(s.data(), ctx);
        }
    };

}
