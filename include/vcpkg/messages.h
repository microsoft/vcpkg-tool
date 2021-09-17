#include <string>
#include <fmt/format.h>
#include <vcpkg/base/system.write.h>

namespace vcpkg::msg
{
    namespace detail
    {
        template <class Tag>
        struct MessageArgument
        {
            const char* name;
            const typename Tag::type* parameter; // always valid
        };
    }
#define DEFINE_MSG_ARG(TYPE, NAME) \
    constexpr static struct NAME ## _t { \
        using type = TYPE; \
        detail::MessageArgument<NAME ## _t> operator=(const TYPE& t) const noexcept \
        { \
            return detail::MessageArgument<NAME ## _t>{#NAME, &t}; \
        } \
    } NAME

    DEFINE_MSG_ARG(StringView, email);
    DEFINE_MSG_ARG(StringView, vcpkg_version);
    DEFINE_MSG_ARG(StringView, error);
    DEFINE_MSG_ARG(StringView, name);
#undef DEFINE_MSG_ARG

    template <class... Tags>
    struct Message
    {
        std::string format(detail::MessageArgument<Tags>... args) const noexcept
        {
            return fmt::format(get_format_string(), fmt::arg(args.name, *args.parameter)...);
        }
        virtual ~Message() = default;
    protected:
        virtual fmt::string_view get_format_string() const noexcept = 0;
    };

    template <class T>
    struct MessageImpl : T::message_type
    {
        fmt::string_view get_format_string() const noexcept override
        {
            return T::default_format_string();
        }
    };

    template <class Message, class... Ts>
    std::string format(Message, Ts... args)
    {
        return MessageImpl<Message>{}.format(args...);
    }

    template <class Message, class... Ts>
    void print_error(Message, Ts... args)
    {
        write_text_to_std_handle(MessageImpl<Message>{}.format(args...), StdHandle::Err);
    }

#define DEFINE_MESSAGE(NAME, DEFAULT_STR, ...) \
    constexpr static struct NAME ## _t { \
        using message_type = Message<__VA_ARGS__>; \
        static fmt::string_view default_format_string() noexcept { \
            return DEFAULT_STR; \
        } \
    } NAME

    DEFINE_MESSAGE(VcpkgHasCrashed, R"(vcpkg.exe has crashed.
Please send an email to:
    {email}
containing a brief summary of what you were trying to do and the following data blob:

Version={vcpkg_version}
EXCEPTION='error'
CMD=
)",
        email_t,
        vcpkg_version_t,
        error_t);
    DEFINE_MESSAGE(VcpkgTestMessage, "Bonjour à {name}\n", name_t);

#undef DEFINE_MESSAGE
}
