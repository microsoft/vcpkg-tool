#include <string>
#include <fmt/format.h>
#include <vcpkg/base/system.print.h>

namespace vcpkg
{
    namespace detail
    {
        template <class Tag>
        struct MessageArgument
        {
            const char* name;
            const typename Tag::type* parameter; // always valid
        };

        template <class... Tags>
        struct MessageCheckFormatArgs
        {
            static constexpr void check_format_args(const detail::MessageArgument<Tags>&...) noexcept { }
        };
    }

    struct MessageContext
    {
        explicit MessageContext(StringView language);

        template <class Message, class... Ts>
        void print(Message m, Ts... args) const
        {
            print(Color::None, m, args...);
        }
        template <class Message, class... Ts>
        void println(Message m, Ts... args) const
        {
            println(Color::None, m, args...);
        }

        template <class Message, class... Ts>
        void print(Color c, Message, Ts... args) const
        {
            Message::check_format_args(args...);
            write_text_to_stdout(c,
                fmt::vformat(
                    get_format_string<Message>(),
                    fmt::arg(args.name, *args.parameter)...
                )
            );
        }
        template <class Message, class... Ts>
        void println(Color c, Message m, Ts... args) const
        {
            print(c, m, args...);
            write_text_to_stdout(Color::None, "\n");
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
#undef DEFINE_MSG_ARG

#define DEFINE_MESSAGE_NOARGS(NAME, DEFAULT_STR) \
    constexpr static struct NAME ## _t : detail::MessageCheckFormatArgs<> { \
        static StringView name() noexcept { \
            return #NAME; \
        } \
        static fmt::string_view default_format_string() noexcept { \
            return DEFAULT_STR; \
        } \
    } NAME
#define DEFINE_MESSAGE(NAME, DEFAULT_STR, ...) \
    constexpr static struct NAME ## _t : detail::MessageCheckFormatArgs<__VA_ARGS__> { \
        static StringView name() noexcept { \
            return #NAME; \
        } \
        static fmt::string_view default_format_string() noexcept { \
            return DEFAULT_STR; \
        } \
    } NAME

    DEFINE_MESSAGE(VcpkgHasCrashed, 
R"(vcpkg.exe has crashed.
Please send an email to:
    {email}
containing a brief summary of what you were trying to do and the following data blob:

Version={vcpkg_version}
EXCEPTION='{error}'
CMD=)",
        email_t,
        vcpkg_version_t,
        error_t);
    DEFINE_MESSAGE_NOARGS(AllRequestedPackagesInstalled, "All requested packages are currently installed.");

#undef DEFINE_MESSAGE

    private:
        template <class Message>
        fmt::string_view get_format_string() const
        {
            auto fsopt = get_dynamic_format_string(Message::name());
            if (auto fstr = fsopt.get())
            {
                return *fstr;
            }
            return Message::default_format_string();
        }
        Optional<fmt::string_view> get_dynamic_format_string(StringView name) const;

        struct MessageContextImpl;
        std::unique_ptr<MessageContextImpl> impl;
    };

}
