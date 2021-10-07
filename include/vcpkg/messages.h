#include <string>
#include <fmt/format.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/commands.interface.h>
#include <vcpkg/base/json.h>

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
        // load from "${SCRIPTS}/locales/${language}.json"
        explicit MessageContext(StringView language);
        // load from the json object
        explicit MessageContext(const Json::Object& message_map);

        template <class Message, class... Ts>
        void print(Message m, Ts... args) const
        {
            print(Color::None, m, args...);
        }
        template <class Message, class... Ts>
        void println(Message m, Ts... args) const
        {
            print(Color::None, m, args...);
            write_text_to_stdout(Color::None, "\n");
        }

        template <class Message, class... Ts>
        void print(Color c, Message, Ts... args) const
        {
            Message::check_format_args(args...);
            auto fmt_string = get_format_string(Message::index);
            write_text_to_stdout(c,
                fmt::vformat(
                    fmt::string_view(fmt_string.begin(), fmt_string.size()),
                    fmt::make_format_args(fmt::arg(args.name, *args.parameter)...)
                )
            );
        }
        template <class Message, class... Ts>
        void println(Color c, Message m, Ts... args) const
        {
            print(c, m, args...);
            write_text_to_stdout(Color::None, "\n");
        }

        static ::size_t last_message_index();

        // REQUIRES: index < last_message_index()
        StringView get_format_string(::size_t index) const;
        // REQUIRES: index < last_message_index()
        static StringView get_message_name(::size_t index);
        // REQUIRES: index < last_message_index()
        static StringView get_default_format_string(::size_t index);
        // REQUIRES: index < last_message_index()
        static StringView get_localization_comment(::size_t index);

    private:
        // returns the id to pass to `get_format_string`
        // should only be called during startup
        static ::size_t _internal_register_message(StringView name, StringView default_format_string, StringView comment);

        struct MessageContextImpl;
        std::unique_ptr<MessageContextImpl> impl;

        template <class Message>
        struct MessageComment
        {
            static StringView comment()
            {
                return "";
            }
        };


    public:
#define DEFINE_MSG_ARG(TYPE, NAME) \
        constexpr static struct NAME ## _t { \
            using type = TYPE; \
            detail::MessageArgument<NAME ## _t> operator=(const TYPE& t) const noexcept \
            { \
                return detail::MessageArgument<NAME ## _t>{#NAME, &t}; \
            } \
        } NAME = {}

        DEFINE_MSG_ARG(StringView, email);
        DEFINE_MSG_ARG(StringView, vcpkg_version);
        DEFINE_MSG_ARG(StringView, error);
#undef DEFINE_MSG_ARG

#define DEFINE_MESSAGE_NOARGS(NAME, COMMENT, DEFAULT_STR) \
    constexpr static struct NAME ## _t : detail::MessageCheckFormatArgs<> { \
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
    } NAME = {}
#define DEFINE_MESSAGE(NAME, COMMENT, DEFAULT_STR, ...) \
    constexpr static struct NAME ## _t : detail::MessageCheckFormatArgs<__VA_ARGS__> { \
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
    } NAME = {}

    DEFINE_MESSAGE(VcpkgHasCrashed, "Don't localize the data blob (the data after the colon)",
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
    DEFINE_MESSAGE_NOARGS(AllRequestedPackagesInstalled, "", "All requested packages are currently installed.");
    DEFINE_MESSAGE_NOARGS(NoLocalizationForMessages, "", "No localization for the following messages:");

#undef ADD_MESSAGE_COMMENT
#undef DEFINE_MESSAGE
#undef DEFINE_MESSAGE_NOARGS
    };

    struct GenerateDefaultMessageMapCommand : Commands::BasicCommand
    {
        void perform_and_exit(const VcpkgCmdArguments&, Filesystem&) const override;
    };
}
