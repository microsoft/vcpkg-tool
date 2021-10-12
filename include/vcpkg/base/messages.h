#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/json.h>
#include <vcpkg/base/fwd/lineinfo.h>

//#include <vcpkg/base/stringliteral.h>

//#include <string>

//#include <fmt/format.h>

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
    struct LocalizedString;
    struct LocalizedStringView;

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
            static constexpr void check_format_args(const Tags&...) noexcept
            {
            }
        };

        LocalizedString internal_vformat(int index, fmt::format_args args);

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

    struct LocalizedStringView
    {
        LocalizedStringView() = default;

        operator StringView() const
        {
            return m_data;
        }
        StringView data() const
        {
            return m_data;
        }

        static LocalizedStringView from_stringview_unchecked(StringView sv)
        {
            LocalizedStringView res;
            res.m_data = sv;
            return res;
        }
    private:
        StringView m_data;
    };

    struct LocalizedString
    {
        LocalizedString() = default;
        operator LocalizedStringView() const
        {
            return LocalizedStringView::from_stringview_unchecked(m_data);
        }
        const std::string& data() const
        {
            return m_data;
        }

        static LocalizedString from_string_unchecked(std::string&& s)
        {
            LocalizedString res;
            res.m_data = std::move(s);
            return res;
        }

        LocalizedString& append(LocalizedStringView sv) &;
        LocalizedString& add_newline() &;
        LocalizedString& appendnl(LocalizedStringView sv) &
        {
            append(sv);
            add_newline();
            return *this;
        }

        LocalizedString&& append(LocalizedStringView sv) && { return std::move(append(sv)); }
        LocalizedString&& add_newline() && { return std::move(add_newline()); }
        LocalizedString&& appendnl(LocalizedStringView sv) && { return std::move(appendnl(sv)); }

    private:
        std::string m_data;
        friend LocalizedString detail::internal_vformat(int index, fmt::format_args args);
    };

    void write_unlocalized_text_to_stdout(Color c, StringView sv);
    inline void write_newline_to_stdout()
    {
        write_unlocalized_text_to_stdout(Color::None, "\n");
    }
    inline void write_text_to_stdout(Color c, LocalizedStringView sv)
    {
        write_unlocalized_text_to_stdout(c, sv);
    }

    template<class Message, class... Tags, class... Ts>
    LocalizedString format(Message, detail::MessageArgument<Tags, Ts>... args)
    {
        // avoid generating code, but still typeck
        // (and avoid unused typedef warnings)
        static_assert((Message::check_format_args((Tags{})...), true), "");
        return detail::internal_vformat(
            Message::index,
            fmt::make_format_args(fmt::arg(Tags::name(), *args.parameter)...));
    }

    template<class Message, class... Ts>
    void print(Message m, Ts... args)
    {
        write_text_to_stdout(Color::None, format(m, args...));
    }
    template<class Message, class... Ts>
    void println(Message m, Ts... args)
    {
        write_text_to_stdout(Color::None, format(m, args...));
        write_newline_to_stdout();
    }

    template<class Message, class... Ts>
    void print(Color c, Message m, Ts... args)
    {
        write_text_to_stdout(c, format(m, args...));
    }
    template<class Message, class... Ts>
    void println(Color c, Message m, Ts... args)
    {
        write_text_to_stdout(c, format(m, args...));
        write_newline_to_stdout();
    }

// these use `constexpr static` instead of `inline` in order to work with GCC 6;
// they are trivial and empty, and their address does not matter, so this is not a problem
#define DECLARE_MSG_ARG(NAME)                                                                                          \
    constexpr static struct NAME##_t                                                                                   \
    {                                                                                                                  \
        constexpr static const char* name() \
        {\
            return #NAME; \
        } \
        template<class T>                                                                                              \
        detail::MessageArgument<NAME##_t, T> operator=(const T& t) const noexcept                                      \
        {                                                                                                              \
            return detail::MessageArgument<NAME##_t, T>{&t};                                                    \
        }                                                                                                              \
    } NAME = {}

    DECLARE_MSG_ARG(url);
    DECLARE_MSG_ARG(email);
    DECLARE_MSG_ARG(vcpkg_version);
    DECLARE_MSG_ARG(error);
    DECLARE_MSG_ARG(triplet);
    DECLARE_MSG_ARG(version);
    DECLARE_MSG_ARG(line_info);
    DECLARE_MSG_ARG(file);
    DECLARE_MSG_ARG(port);
    DECLARE_MSG_ARG(option);
    DECLARE_MSG_ARG(sha);
    DECLARE_MSG_ARG(name);
    DECLARE_MSG_ARG(value);
    DECLARE_MSG_ARG(old_value);
    DECLARE_MSG_ARG(new_value);
    DECLARE_MSG_ARG(expected_value);
    DECLARE_MSG_ARG(found_value);
    DECLARE_MSG_ARG(exit_code);
    DECLARE_MSG_ARG(http_code);
#undef DECLARE_MSG_ARG

// These are `...` instead of 
#define DECLARE_MESSAGE(NAME, ARGS, COMMENT, ...)                                                              \
    constexpr struct NAME ## _msg_t : decltype(::vcpkg::msg::detail::make_message_check_format_args ARGS)             \
    {                                                                                                                  \
        static ::vcpkg::StringLiteral name() { return #NAME; }                                                         \
        static ::vcpkg::StringLiteral localization_comment() { return COMMENT; };                                      \
        static ::vcpkg::StringLiteral default_format_string() noexcept { return __VA_ARGS__; }                         \
        static const ::size_t index;                                                                                   \
    } msg##NAME = {}
#define REGISTER_MESSAGE(NAME)                                                                                         \
    const ::size_t NAME##_msg_t ::index = ::vcpkg::msg::detail::startup_register_message(                              \
        NAME##_msg_t::name(), NAME##_msg_t::default_format_string(), NAME##_msg_t::localization_comment())
#define DECLARE_AND_REGISTER_MESSAGE(NAME, ARGS, COMMENT, ...)                                                 \
    DECLARE_MESSAGE(NAME, ARGS, COMMENT, __VA_ARGS__);                                                                 \
    REGISTER_MESSAGE(NAME)
}

template <>
struct fmt::formatter<vcpkg::msg::LocalizedString> : fmt::formatter<vcpkg::StringView>
{
    // parse is inherited from formatter<StringView>
    template<class FormatContext>
    auto format(const vcpkg::msg::LocalizedString& s, FormatContext& ctx)
    {
        return fmt::formatter<vcpkg::StringView>::format(s.data(), ctx);
    }
};
template <>
struct fmt::formatter<vcpkg::msg::LocalizedStringView> : fmt::formatter<vcpkg::StringView>
{
    // parse is inherited from formatter<StringView>
    template<class FormatContext>
    auto format(vcpkg::msg::LocalizedStringView sv, FormatContext& ctx)
    {
        return fmt::formatter<vcpkg::StringView>::format(sv, ctx);
    }
};
