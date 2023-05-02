﻿#pragma once

#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/base/fmt.h>
#include <vcpkg/base/format.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>

#include <string>
#include <type_traits>
#include <vector>

namespace vcpkg
{
    template<class T>
    struct identity
    {
        using type = T;
    };
    template<class T>
    using identity_t = typename identity<T>::type;
}

#define VCPKG_DECL_MSG_TEMPLATE class... MessageTags, class... MessageTypes
#define VCPKG_DECL_MSG_ARGS                                                                                            \
    ::vcpkg::msg::MessageT<MessageTags...> _message_token,                                                             \
        ::vcpkg::msg::TagArg<::vcpkg::identity_t<MessageTags>, MessageTypes>... _message_args
#define VCPKG_EXPAND_MSG_ARGS _message_token, _message_args...

namespace vcpkg::msg
{
    namespace detail
    {
        template<class... Tags>
        struct MessageT<Tags...> make_message_base(Tags...);

        LocalizedString format_message_by_index(size_t index, fmt::format_args args);
        void format_message_by_index_to(LocalizedString& s, size_t index, fmt::format_args args);
    }
    template<class Tag, class Type>
    struct TagArg
    {
        static_assert(!std::is_constructible<StringView, Type>::value);
        const Type& t;
        auto arg() const { return fmt::arg(Tag::name.c_str(), t); }
    };
    template<class Tag>
    struct TagArg<Tag, StringView>
    {
        StringView const t;
        auto arg() const { return fmt::arg(Tag::name.c_str(), t); }
    };

    template<class Type>
    using StringViewable = std::conditional_t<std::is_constructible<StringView, Type>::value, StringView, Type>;

    template<class... Tags>
    struct MessageT
    {
        const size_t index;
    };

    template<VCPKG_DECL_MSG_TEMPLATE>
    LocalizedString format(VCPKG_DECL_MSG_ARGS);
    template<VCPKG_DECL_MSG_TEMPLATE>
    void format_to(LocalizedString&, VCPKG_DECL_MSG_ARGS);

    extern template LocalizedString format<>(MessageT<>);
    extern template void format_to<>(LocalizedString&, MessageT<>);
}

namespace vcpkg
{
    struct LocalizedString
    {
        LocalizedString() = default;
        operator StringView() const noexcept;
        const std::string& data() const noexcept;
        const std::string& to_string() const noexcept;
        std::string extract_data();

        template<class T, std::enable_if_t<std::is_same<char, T>::value, int> = 0>
        static LocalizedString from_raw(std::basic_string<T>&& s) noexcept;
        static LocalizedString from_raw(StringView s);

        LocalizedString& append_raw(char c);
        LocalizedString& append_raw(StringView s);
        template<class T, class = decltype(std::declval<const T&>().to_string(std::declval<std::string&>()))>
        LocalizedString& append_raw(const T& s)
        {
            s.to_string(m_data);
            return *this;
        }
        LocalizedString& append(const LocalizedString& s);
        template<VCPKG_DECL_MSG_TEMPLATE>
        LocalizedString& append(VCPKG_DECL_MSG_ARGS)
        {
            msg::format_to(*this, VCPKG_EXPAND_MSG_ARGS);
            return *this;
        }
        LocalizedString& append_indent(size_t indent = 1);

        // 0 items - Does nothing
        // 1 item - .append_raw(' ').append(item)
        // 2+ items - foreach: .append_raw('\n').append_indent(indent).append(item)
        LocalizedString& append_floating_list(int indent, View<LocalizedString> items);
        friend bool operator==(const LocalizedString& lhs, const LocalizedString& rhs) noexcept;
        friend bool operator!=(const LocalizedString& lhs, const LocalizedString& rhs) noexcept;
        friend bool operator<(const LocalizedString& lhs, const LocalizedString& rhs) noexcept;
        friend bool operator<=(const LocalizedString& lhs, const LocalizedString& rhs) noexcept;
        friend bool operator>(const LocalizedString& lhs, const LocalizedString& rhs) noexcept;
        friend bool operator>=(const LocalizedString& lhs, const LocalizedString& rhs) noexcept;
        bool empty() const noexcept;
        void clear() noexcept;

        friend void msg::detail::format_message_by_index_to(LocalizedString& s, size_t index, fmt::format_args args);

    private:
        std::string m_data;

        explicit LocalizedString(StringView data);
        explicit LocalizedString(std::string&& data) noexcept;
    };
}

VCPKG_FORMAT_AS(vcpkg::LocalizedString, vcpkg::StringView);

namespace vcpkg::msg
{
    template<class... Tags, class... Types>
    LocalizedString format(MessageT<Tags...> m, TagArg<identity_t<Tags>, Types>... args)
    {
        return detail::format_message_by_index(m.index, fmt::make_format_args(args.arg()...));
    }
    template<class... Tags, class... Types>
    void format_to(LocalizedString& s, MessageT<Tags...> m, TagArg<identity_t<Tags>, Types>... args)
    {
        return detail::format_message_by_index_to(s, m.index, fmt::make_format_args(args.arg()...));
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

    [[nodiscard]] LocalizedString format_error();
    [[nodiscard]] LocalizedString format_error(const LocalizedString& s);
    template<VCPKG_DECL_MSG_TEMPLATE>
    [[nodiscard]] LocalizedString format_error(VCPKG_DECL_MSG_ARGS)
    {
        auto s = format_error();
        msg::format_to(s, VCPKG_EXPAND_MSG_ARGS);
        return s;
    }
    void println_error(const LocalizedString& s);
    template<VCPKG_DECL_MSG_TEMPLATE>
    void println_error(VCPKG_DECL_MSG_ARGS)
    {
        auto s = format_error();
        msg::format_to(s, VCPKG_EXPAND_MSG_ARGS);
        println(Color::error, s);
    }

    [[nodiscard]] LocalizedString format_warning();
    [[nodiscard]] LocalizedString format_warning(const LocalizedString& s);
    template<VCPKG_DECL_MSG_TEMPLATE>
    [[nodiscard]] LocalizedString format_warning(VCPKG_DECL_MSG_ARGS)
    {
        auto s = format_warning();
        msg::format_to(s, VCPKG_EXPAND_MSG_ARGS);
        return s;
    }
    void println_warning(const LocalizedString& s);
    template<VCPKG_DECL_MSG_TEMPLATE>
    void println_warning(VCPKG_DECL_MSG_ARGS)
    {
        auto s = format_warning();
        msg::format_to(s, VCPKG_EXPAND_MSG_ARGS);
        println(Color::warning, s);
    }

    template<VCPKG_DECL_MSG_TEMPLATE>
    void print(VCPKG_DECL_MSG_ARGS)
    {
        print(msg::format(VCPKG_EXPAND_MSG_ARGS));
    }
    template<VCPKG_DECL_MSG_TEMPLATE>
    void print(Color c, VCPKG_DECL_MSG_ARGS)
    {
        print(c, msg::format(VCPKG_EXPAND_MSG_ARGS));
    }
    template<VCPKG_DECL_MSG_TEMPLATE>
    void println(VCPKG_DECL_MSG_ARGS)
    {
        println(msg::format(VCPKG_EXPAND_MSG_ARGS));
    }
    template<VCPKG_DECL_MSG_TEMPLATE>
    void println(Color c, VCPKG_DECL_MSG_ARGS)
    {
        println(c, msg::format(VCPKG_EXPAND_MSG_ARGS));
    }

#define DECLARE_MSG_ARG(NAME, EXAMPLE)                                                                                 \
    static constexpr struct NAME##_t                                                                                   \
    {                                                                                                                  \
        static const ::vcpkg::StringLiteral name;                                                                      \
        template<class T>                                                                                              \
        TagArg<NAME##_t, StringViewable<T>> operator=(const T& t) const noexcept                                       \
        {                                                                                                              \
            return TagArg<NAME##_t, StringViewable<T>>{t};                                                             \
        }                                                                                                              \
    } NAME = {};

#include <vcpkg/base/message-args.inc.h>

#undef DECLARE_MSG_ARG
}
namespace vcpkg
{

#define DECLARE_MESSAGE(NAME, ARGS, COMMENT, ...)                                                                      \
    extern const decltype(::vcpkg::msg::detail::make_message_base ARGS) msg##NAME;

#include <vcpkg/base/message-data.inc.h>
#undef DECLARE_MESSAGE

    namespace msg
    {
        extern const decltype(vcpkg::msgErrorMessage) msgErrorMessage;
        extern const decltype(vcpkg::msgWarningMessage) msgWarningMessage;
        extern const decltype(vcpkg::msgNoteMessage) msgNoteMessage;
        extern const decltype(vcpkg::msgSeeURL) msgSeeURL;
        extern const decltype(vcpkg::msgInternalErrorMessage) msgInternalErrorMessage;
        extern const decltype(vcpkg::msgInternalErrorMessageContact) msgInternalErrorMessageContact;
    }
}
