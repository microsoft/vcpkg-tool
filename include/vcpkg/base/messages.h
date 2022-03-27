#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/json.h>
#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/base/format.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/stringliteral.h>

#include <string>
#include <utility>

namespace vcpkg
{
    namespace msg::detail
    {
        template<class Tag, class Type>
        struct MessageArgument;
    }
    namespace msg
    {
        template<class Message, class... Tags, class... Ts>
        LocalizedString format(Message, detail::MessageArgument<Tags, Ts>... args);
    }

    struct LocalizedString
    {
        LocalizedString() = default;
        operator StringView() const { return m_data; }
        const std::string& data() const { return m_data; }
        std::string extract_data() { return std::exchange(m_data, ""); }

        static LocalizedString from_raw(const std::string& s) { return LocalizedString(s); }
        static LocalizedString from_raw(std::string&& s) { return LocalizedString(std::move(s)); }

        LocalizedString& append_raw(StringView s)
        {
            m_data.append(s.begin(), s.end());
            return *this;
        }
        LocalizedString& append(const LocalizedString& s)
        {
            m_data.append(s.m_data);
            return *this;
        }
        template<class Message, class... Args>
        LocalizedString& append(Message m, const Args&... args)
        {
            return append(msg::format(m, args...));
        }

        LocalizedString& appendnl()
        {
            m_data.push_back('\n');
            return *this;
        }
        LocalizedString& append_indent()
        {
            static const char indent[] = "    ";
            m_data.append(indent, indent + sizeof(indent) - 1);
            return *this;
        }

        friend const char* to_printf_arg(const LocalizedString& s) { return s.data().c_str(); }

        friend bool operator==(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() == rhs.data();
        }

        friend bool operator!=(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() != rhs.data();
        }

        friend bool operator<(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() < rhs.data();
        }

        friend bool operator<=(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() <= rhs.data();
        }

        friend bool operator>(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() > rhs.data();
        }

        friend bool operator>=(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() >= rhs.data();
        }

    private:
        std::string m_data;

        explicit LocalizedString(const std::string& data) : m_data(data) { }
        explicit LocalizedString(std::string&& data) : m_data(std::move(data)) { }
    };
}

template<>
struct fmt::formatter<vcpkg::LocalizedString> : fmt::formatter<vcpkg::StringView>
{
    // parse is inherited from formatter<StringView>
    template<class FormatContext>
    auto format(const vcpkg::LocalizedString& s, FormatContext& ctx)
    {
        return formatter<vcpkg::StringView>::format(s.data(), ctx);
    }
};

namespace vcpkg::msg
{
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

        template<class... Args>
        std::string get_examples_for_args(StringView extra, const MessageCheckFormatArgs<Args...>&)
        {
            std::string res(extra.begin(), extra.end());
            if (res == "{Locked}") return res;

            if (!res.empty()) res.push_back('\n');
            (void)(..., res.append(Args::comment()));
            return res;
        }

        ::size_t startup_register_message(StringLiteral name, StringLiteral format_string, std::string&& comment);

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

    // load from "locale_base/messages.${language}.json"
    void threadunsafe_initialize_context(const Filesystem& fs, StringView language, const Path& locale_base);
    // initialize without any localized messages (use default messages only)
    void threadunsafe_initialize_context();

    template<class Message, class... Tags, class... Ts>
    LocalizedString format(Message, detail::MessageArgument<Tags, Ts>... args)
    {
        // avoid generating code, but still typeck
        // (and avoid unused typedef warnings)
        static_assert((Message::check_format_args((Tags{})...), true), "");
        return detail::internal_vformat(Message::index,
                                        fmt::make_format_args(fmt::arg(Tags::name(), *args.parameter)...));
    }

    inline void println() { msg::write_unlocalized_text_to_stdout(Color::none, "\n"); }

    inline void print(Color c, const LocalizedString& s) { msg::write_unlocalized_text_to_stdout(c, s); }
    inline void print(const LocalizedString& s) { msg::write_unlocalized_text_to_stdout(Color::none, s); }
    inline void println(Color c, const LocalizedString& s)
    {
        msg::write_unlocalized_text_to_stdout(c, s);
        msg::write_unlocalized_text_to_stdout(Color::none, "\n");
    }
    inline void println(const LocalizedString& s)
    {
        msg::write_unlocalized_text_to_stdout(Color::none, s);
        msg::write_unlocalized_text_to_stdout(Color::none, "\n");
    }

    template<class Message, class... Ts>
    void print(Message m, Ts... args)
    {
        print(format(m, args...));
    }
    template<class Message, class... Ts>
    void println(Message m, Ts... args)
    {
        print(format(m, args...).appendnl());
    }

    template<class Message, class... Ts>
    void print(Color c, Message m, Ts... args)
    {
        print(c, format(m, args...));
    }
    template<class Message, class... Ts>
    void println(Color c, Message m, Ts... args)
    {
        print(c, format(m, args...).appendnl());
    }

// these use `constexpr static` instead of `inline` in order to work with GCC 6;
// they are trivial and empty, and their address does not matter, so this is not a problem
#define DECLARE_MSG_ARG(NAME, EXAMPLE)                                                                                 \
    constexpr static struct NAME##_t                                                                                   \
    {                                                                                                                  \
        constexpr static const char* name() { return #NAME; }                                                          \
        constexpr static const char* comment()                                                                         \
        {                                                                                                              \
            return sizeof(EXAMPLE) > 1 ? "example of {" #NAME "} is '" EXAMPLE "'.\n" : "";                            \
        }                                                                                                              \
        template<class T>                                                                                              \
        detail::MessageArgument<NAME##_t, T> operator=(const T& t) const noexcept                                      \
        {                                                                                                              \
            return detail::MessageArgument<NAME##_t, T>{&t};                                                           \
        }                                                                                                              \
    } NAME = {}

    DECLARE_MSG_ARG(error, "");
    DECLARE_MSG_ARG(value, "");
    DECLARE_MSG_ARG(pretty_value, "");
    DECLARE_MSG_ARG(expected, "");
    DECLARE_MSG_ARG(actual, "");
    DECLARE_MSG_ARG(list, "");

    DECLARE_MSG_ARG(actual_version, "1.3.8");
    DECLARE_MSG_ARG(arch, "x64");
    DECLARE_MSG_ARG(build_result, "One of the BuildResultXxx messages (such as BuildResultSucceeded/SUCCEEDED)");
    DECLARE_MSG_ARG(column, "42");
    DECLARE_MSG_ARG(command_line, "vcpkg install zlib");
    DECLARE_MSG_ARG(command_name, "install");
    DECLARE_MSG_ARG(count, "42");
    DECLARE_MSG_ARG(elapsed, "3.532 min");
    DECLARE_MSG_ARG(email, "vcpkg@microsoft.com");
    DECLARE_MSG_ARG(exit_code, "127");
    DECLARE_MSG_ARG(expected_version, "1.3.8");
    DECLARE_MSG_ARG(new_scheme, "version");
    DECLARE_MSG_ARG(old_scheme, "version-string");
    DECLARE_MSG_ARG(option, "editable");
    DECLARE_MSG_ARG(package_name, "zlib");
    DECLARE_MSG_ARG(path, "/foo/bar");
    DECLARE_MSG_ARG(row, "42");
    DECLARE_MSG_ARG(spec, "zlib:x64-windows");
    DECLARE_MSG_ARG(system_name, "Darwin");
    DECLARE_MSG_ARG(triplet, "x64-windows");
    DECLARE_MSG_ARG(url, "https://github.com/microsoft/vcpkg");
    DECLARE_MSG_ARG(version, "1.3.8");
    DECLARE_MSG_ARG(vcpkg_line_info, "/a/b/foo.cpp(13)");
#undef DECLARE_MSG_ARG

// These are `...` instead of
#define DECLARE_MESSAGE(NAME, ARGS, COMMENT, ...)                                                                      \
    constexpr struct NAME##_msg_t : decltype(::vcpkg::msg::detail::make_message_check_format_args ARGS)                \
    {                                                                                                                  \
        using is_message_type = void;                                                                                  \
        static ::vcpkg::StringLiteral name() { return #NAME; }                                                         \
        static ::vcpkg::StringLiteral extra_comment() { return COMMENT; }                                              \
        static ::std::string full_comment();                                                                           \
        static ::vcpkg::StringLiteral default_format_string() noexcept { return __VA_ARGS__; }                         \
        static const ::size_t index;                                                                                   \
    } msg##NAME VCPKG_UNUSED = {}
#define REGISTER_MESSAGE(NAME)                                                                                         \
    ::std::string NAME##_msg_t::full_comment()                                                                         \
    {                                                                                                                  \
        return ::vcpkg::msg::detail::get_examples_for_args(NAME##_msg_t::extra_comment(), NAME##_msg_t{});             \
    }                                                                                                                  \
    const ::size_t NAME##_msg_t::index = ::vcpkg::msg::detail::startup_register_message(                               \
        NAME##_msg_t::name(), NAME##_msg_t::default_format_string(), NAME##_msg_t::full_comment())
#define DECLARE_AND_REGISTER_MESSAGE(NAME, ARGS, COMMENT, ...)                                                         \
    DECLARE_MESSAGE(NAME, ARGS, COMMENT, __VA_ARGS__);                                                                 \
    REGISTER_MESSAGE(NAME)

    DECLARE_MESSAGE(SeeURL, (msg::url), "", "See {url} for more information.");
    DECLARE_MESSAGE(NoteMessage, (), "", "note: ");
    DECLARE_MESSAGE(WarningMessage, (), "", "warning: ");
    DECLARE_MESSAGE(ErrorMessage, (), "", "error: ");
    DECLARE_MESSAGE(InternalErrorMessage, (), "", "internal error: ");
    DECLARE_MESSAGE(
        InternalErrorMessageContact,
        (),
        "",
        "Please open an issue at "
        "https://github.com/microsoft/vcpkg/issues/new?template=other-type-of-bug-report.md&labels=category:vcpkg-bug "
        "with detailed steps to reproduce the problem.");
    DECLARE_MESSAGE(BothYesAndNoOptionSpecifiedError,
                    (msg::option),
                    "",
                    "error: cannot specify both --no-{option} and --{option}.");

    template<class Message, class... Ts>
    void print_warning(Message m, Ts... args)
    {
        print(Color::warning, format(msgWarningMessage).append(format(m, args...).appendnl()));
    }

    template<class Message, class... Ts>
    void print_error(Message m, Ts... args)
    {
        print(Color::error, format(msgErrorMessage).append(format(m, args...).appendnl()));
    }
}
