#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/setup-messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <iterator>
#include <vector>

#include <cmrc/cmrc.hpp>

CMRC_DECLARE(cmakerc);

using namespace vcpkg;

namespace vcpkg
{
    LocalizedString::operator StringView() const noexcept { return m_data; }
    const std::string& LocalizedString::data() const noexcept { return m_data; }
    const std::string& LocalizedString::to_string() const noexcept { return m_data; }
    std::string LocalizedString::extract_data() { return std::exchange(m_data, std::string{}); }

    template<class T, std::enable_if_t<std::is_same<char, T>::value, int>>
    LocalizedString LocalizedString::from_raw(std::basic_string<T>&& s) noexcept
    {
        return LocalizedString(std::move(s));
    }
    template LocalizedString LocalizedString::from_raw<char>(std::basic_string<char>&& s) noexcept;
    LocalizedString LocalizedString::from_raw(StringView s) { return LocalizedString(s); }

    LocalizedString& LocalizedString::append_raw(char c) &
    {
        m_data.push_back(c);
        return *this;
    }

    LocalizedString&& LocalizedString::append_raw(char c) && { return std::move(append_raw(c)); }

    LocalizedString& LocalizedString::append_raw(StringView s) &
    {
        m_data.append(s.begin(), s.size());
        return *this;
    }

    LocalizedString&& LocalizedString::append_raw(StringView s) && { return std::move(append_raw(s)); }

    LocalizedString& LocalizedString::append(const LocalizedString& s) &
    {
        m_data.append(s.m_data);
        return *this;
    }

    LocalizedString&& LocalizedString::append(const LocalizedString& s) && { return std::move(append(s)); }

    LocalizedString& LocalizedString::append_indent(size_t indent) &
    {
        m_data.append(indent * 2, ' ');
        return *this;
    }

    LocalizedString&& LocalizedString::append_indent(size_t indent) && { return std::move(append_indent(indent)); }

    LocalizedString& LocalizedString::append_floating_list(int indent, View<LocalizedString> items) &
    {
        switch (items.size())
        {
            case 0: break;
            case 1: append_raw(' ').append(items[0]); break;
            default:
                for (auto&& item : items)
                {
                    append_raw('\n').append_indent(indent).append(item);
                }

                break;
        }

        return *this;
    }

    LocalizedString&& LocalizedString::append_floating_list(int indent, View<LocalizedString> items) &&
    {
        return std::move(append_floating_list(indent, items));
    }

    bool operator==(const LocalizedString& lhs, const LocalizedString& rhs) noexcept
    {
        return lhs.data() == rhs.data();
    }

    bool operator!=(const LocalizedString& lhs, const LocalizedString& rhs) noexcept
    {
        return lhs.data() != rhs.data();
    }

    bool operator<(const LocalizedString& lhs, const LocalizedString& rhs) noexcept { return lhs.data() < rhs.data(); }

    bool operator<=(const LocalizedString& lhs, const LocalizedString& rhs) noexcept
    {
        return lhs.data() <= rhs.data();
    }

    bool operator>(const LocalizedString& lhs, const LocalizedString& rhs) noexcept { return lhs.data() > rhs.data(); }

    bool operator>=(const LocalizedString& lhs, const LocalizedString& rhs) noexcept
    {
        return lhs.data() >= rhs.data();
    }

    bool LocalizedString::empty() const noexcept { return m_data.empty(); }
    void LocalizedString::clear() noexcept { m_data.clear(); }

    LocalizedString::LocalizedString(StringView data) : m_data(data.data(), data.size()) { }
    LocalizedString::LocalizedString(std::string&& data) noexcept : m_data(std::move(data)) { }

    LocalizedString format_environment_variable(StringView variable_name)
    {
#if defined(_WIN32)
        return LocalizedString::from_raw(fmt::format("%{}%", variable_name));
#else  // ^^^ _WIN32 / !_WIN32 vvv
        return LocalizedString::from_raw(fmt::format("${}", variable_name));
#endif // ^^^ !_WIN32
    }

    LocalizedString error_prefix() { return LocalizedString::from_raw(ErrorPrefix); }
    LocalizedString internal_error_prefix() { return LocalizedString::from_raw(InternalErrorPrefix); }
    LocalizedString message_prefix() { return LocalizedString::from_raw(MessagePrefix); }
    LocalizedString note_prefix() { return LocalizedString::from_raw(NotePrefix); }
    LocalizedString warning_prefix() { return LocalizedString::from_raw(WarningPrefix); }
}

namespace vcpkg::msg
{
    template LocalizedString format<>(MessageT<>);
    template void format_to<>(LocalizedString&, MessageT<>);
}
namespace
{
    template<class T>
    struct ArgExample;

#define DECLARE_MSG_ARG(NAME, EXAMPLE)                                                                                 \
    template<>                                                                                                         \
    struct ArgExample<::vcpkg::msg::NAME##_t>                                                                          \
    {                                                                                                                  \
        static constexpr StringLiteral example = sizeof(EXAMPLE) > 1 ? StringLiteral(#NAME "} is " EXAMPLE)            \
                                                                     : StringLiteral("");                              \
    };
#include <vcpkg/base/message-args.inc.h>
#undef DECLARE_MSG_ARG
}
#define DECLARE_MSG_ARG(NAME, EXAMPLE) const StringLiteral vcpkg::msg::NAME##_t::name = #NAME;
#include <vcpkg/base/message-args.inc.h>
#undef DECLARE_MSG_ARG

namespace vcpkg
{
    namespace
    {
        static constexpr const size_t max_number_of_args = 5;

        struct MessageData
        {
            StringLiteral name;
            std::array<const StringLiteral*, max_number_of_args> arg_examples;
            const char* comment;
            StringLiteral builtin_message;
        };

        template<class... Args>
        constexpr std::array<const StringLiteral*, max_number_of_args> make_arg_examples_array(Args...)
        {
            return std::array<const StringLiteral*, max_number_of_args>{&ArgExample<Args>::example...};
        }

        constexpr MessageData message_data[] = {
#define DECLARE_MESSAGE(NAME, ARGS, COMMENT, ...) {#NAME, make_arg_examples_array ARGS, COMMENT, __VA_ARGS__},
#include <vcpkg/base/message-data.inc.h>
#undef DECLARE_MESSAGE
        };
    }

    namespace msg::detail
    {
        static constexpr const size_t number_of_messages = std::size(message_data);
    }
    static std::string* loaded_localization_data = 0;
    static const char* loaded_localization_file_begin = 0;
    static const char* loaded_localization_file_end = 0;

    namespace
    {
        enum class message_index
        {
#define DECLARE_MESSAGE(NAME, ARGS, COMMENT, ...) NAME,
#include <vcpkg/base/message-data.inc.h>
#undef DECLARE_MESSAGE
        };
    }

    namespace msg
    {
        namespace
        {
            std::string get_localization_comment(::size_t index)
            {
                if (index >= detail::number_of_messages) Checks::unreachable(VCPKG_LINE_INFO);
                std::string msg = message_data[index].comment;
                for (auto&& ex : message_data[index].arg_examples)
                {
                    if (ex == nullptr || ex->empty()) continue;
                    if (!msg.empty()) msg.push_back(' ');
                    msg.append("An example of {");
                    msg.append(ex->data(), ex->size());
                    msg.push_back('.');
                }
                return msg;
            }
        }

        std::vector<RawMessage> get_sorted_english_messages()
        {
            struct MessageSorter
            {
                bool operator()(const RawMessage& lhs, const RawMessage& rhs) const { return lhs.name < rhs.name; }
            };

            std::vector<RawMessage> messages(msg::detail::number_of_messages);
            for (size_t index = 0; index < msg::detail::number_of_messages; ++index)
            {
                auto& msg = messages[index];
                msg.name = message_data[index].name;
                msg.value = message_data[index].builtin_message;
                msg.comment = get_localization_comment(index);
            }
            std::sort(messages.begin(), messages.end(), MessageSorter{});
            return messages;
        }

        void detail::format_message_by_index_to(LocalizedString& s, size_t index, fmt::format_args args)
        {
            if (index >= detail::number_of_messages) Checks::unreachable(VCPKG_LINE_INFO);
            try
            {
                if (loaded_localization_data)
                {
                    fmt::vformat_to(std::back_inserter(s.m_data), loaded_localization_data[index], args);
                    return;
                }
            }
            catch (const fmt::format_error&)
            {
                Debug::println("Failed to use localized message ", message_data[index].name);
            }
            const auto default_format_string = message_data[index].builtin_message;
            try
            {
                fmt::vformat_to(
                    std::back_inserter(s.m_data), {default_format_string.data(), default_format_string.size()}, args);
                return;
            }
            catch (const fmt::format_error&)
            {
            }
            msg::write_unlocalized_text_to_stdout(
                Color::error,
                fmt::format("INTERNAL ERROR: failed to format default format string for index {}\nformat string: {}\n",
                            index,
                            default_format_string));
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        LocalizedString detail::format_message_by_index(size_t index, fmt::format_args args)
        {
            LocalizedString s;
            format_message_by_index_to(s, index, args);
            return s;
        }
    }

#define DECLARE_MESSAGE(NAME, ARGS, COMMENT, ...)                                                                      \
    const decltype(::vcpkg::msg::detail::make_message_base ARGS) msg##NAME{static_cast<size_t>(message_index::NAME)};

#include <vcpkg/base/message-data.inc.h>
#undef DECLARE_MESSAGE
}
namespace vcpkg::msg
{
    // basic implementation - the write_unlocalized_text_to_stdout
#if defined(_WIN32)
    static bool is_console(HANDLE h)
    {
        DWORD mode = 0;
        // GetConsoleMode succeeds iff `h` is a console
        // we do not actually care about the mode of the console
        return GetConsoleMode(h, &mode);
    }

    static void check_write(BOOL success)
    {
        if (!success)
        {
            ::fwprintf(stderr, L"[DEBUG] Failed to write to stdout: %lu\n", GetLastError());
            std::abort();
        }
    }
    static DWORD size_to_write(::size_t size) { return size > MAXDWORD ? MAXDWORD : static_cast<DWORD>(size); }

    static void write_unlocalized_text_impl(Color c, StringView sv, HANDLE the_handle, bool is_console)
    {
        if (sv.empty()) return;

        if (is_console)
        {
            WORD original_color = 0;
            if (c != Color::none)
            {
                CONSOLE_SCREEN_BUFFER_INFO console_screen_buffer_info{};
                ::GetConsoleScreenBufferInfo(the_handle, &console_screen_buffer_info);
                original_color = console_screen_buffer_info.wAttributes;
                ::SetConsoleTextAttribute(the_handle, static_cast<WORD>(c) | (original_color & 0xF0));
            }

            auto as_wstr = Strings::to_utf16(sv);

            const wchar_t* pointer = as_wstr.data();
            ::size_t size = as_wstr.size();

            while (size != 0)
            {
                DWORD written = 0;
                check_write(::WriteConsoleW(the_handle, pointer, size_to_write(size), &written, nullptr));
                pointer += written;
                size -= written;
            }

            if (c != Color::none)
            {
                ::SetConsoleTextAttribute(the_handle, original_color);
            }
        }
        else
        {
            const char* pointer = sv.data();
            ::size_t size = sv.size();

            while (size != 0)
            {
                DWORD written = 0;
                check_write(::WriteFile(the_handle, pointer, size_to_write(size), &written, nullptr));
                pointer += written;
                size -= written;
            }
        }
    }

    void write_unlocalized_text_to_stdout(Color c, StringView sv)
    {
        static const HANDLE stdout_handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
        static const bool stdout_is_console = is_console(stdout_handle);
        return write_unlocalized_text_impl(c, sv, stdout_handle, stdout_is_console);
    }

    void write_unlocalized_text_to_stderr(Color c, StringView sv)
    {
        static const HANDLE stderr_handle = ::GetStdHandle(STD_ERROR_HANDLE);
        static const bool stderr_is_console = is_console(stderr_handle);
        return write_unlocalized_text_impl(c, sv, stderr_handle, stderr_is_console);
    }
#else
    static void write_all(const char* ptr, size_t to_write, int fd)
    {
        while (to_write != 0)
        {
            auto written = ::write(fd, ptr, to_write);
            if (written == -1)
            {
                ::fprintf(stderr, "[DEBUG] Failed to print to stdout: %d\n", errno);
                std::abort();
            }
            ptr += written;
            to_write -= written;
        }
    }

    static void write_unlocalized_text_impl(Color c, StringView sv, int fd, bool is_a_tty)
    {
        static constexpr char reset_color_sequence[] = {'\033', '[', '0', 'm'};

        if (sv.empty()) return;

        bool reset_color = false;
        if (is_a_tty && c != Color::none)
        {
            reset_color = true;

            const char set_color_sequence[] = {'\033', '[', '9', static_cast<char>(c), 'm'};
            write_all(set_color_sequence, sizeof(set_color_sequence), fd);
        }

        write_all(sv.data(), sv.size(), fd);

        if (reset_color)
        {
            write_all(reset_color_sequence, sizeof(reset_color_sequence), fd);
        }
    }

    void write_unlocalized_text_to_stdout(Color c, StringView sv)
    {
        static bool is_a_tty = ::isatty(STDOUT_FILENO);
        return write_unlocalized_text_impl(c, sv, STDOUT_FILENO, is_a_tty);
    }

    void write_unlocalized_text_to_stderr(Color c, StringView sv)
    {
        static bool is_a_tty = ::isatty(STDERR_FILENO);
        return write_unlocalized_text_impl(c, sv, STDERR_FILENO, is_a_tty);
    }
#endif

    void load_from_message_map(const MessageMapAndFile& map_and_file)
    {
        auto&& message_map = map_and_file.map;

        std::unique_ptr<std::string[]> a = std::make_unique<std::string[]>(detail::number_of_messages);
        for (size_t i = 0; i < detail::number_of_messages; ++i)
        {
            if (auto p = message_map.get(message_data[i].name))
            {
                a[i] = p->string(VCPKG_LINE_INFO).to_string();
            }
            else
            {
                a[i] = message_data[i].builtin_message.to_string();
            }
        }

        loaded_localization_file_begin = map_and_file.map_file.begin();
        loaded_localization_file_end = map_and_file.map_file.end();
        loaded_localization_data = a.release();
    }

    StringView get_loaded_file() { return {loaded_localization_file_begin, loaded_localization_file_end}; }

    ExpectedL<MessageMapAndFile> get_message_map_from_lcid(int LCID)
    {
        auto embedded_filesystem = cmrc::cmakerc::get_filesystem();

        const auto maybe_locale_path = get_locale_path(LCID);
        if (const auto locale_path = maybe_locale_path.get())
        {
            auto file = embedded_filesystem.open(*locale_path);
            StringView sv{file.begin(), file.end()};
            return Json::parse_object(sv, *locale_path).map([&](Json::Object&& parsed_file) {
                return MessageMapAndFile{std::move(parsed_file), sv};
            });
        }

        // this is called during localization setup so it can't be localized
        return LocalizedString::from_raw("Unrecognized LCID");
    }

    Optional<std::string> get_locale_path(int LCID)
    {
        return get_language_tag(LCID).map(
            [](StringLiteral tag) { return fmt::format("locales/messages.{}.json", tag); });
    }

    // LCIDs supported by VS:
    // https://learn.microsoft.com/visualstudio/ide/reference/lcid-devenv-exe?view=vs-2022
    Optional<StringLiteral> get_language_tag(int LCID)
    {
        static constexpr std::pair<int, StringLiteral> languages[] = {
            std::pair<int, StringLiteral>(1029, "cs"), // Czech
            std::pair<int, StringLiteral>(1031, "de"), // German
            // Always use default handling for 1033 (English)
            // std::pair<int, StringLiteral>(1033, "en"),    // English
            std::pair<int, StringLiteral>(3082, "es"),       // Spanish (Spain)
            std::pair<int, StringLiteral>(1036, "fr"),       // French
            std::pair<int, StringLiteral>(1040, "it"),       // Italian
            std::pair<int, StringLiteral>(1041, "ja"),       // Japanese
            std::pair<int, StringLiteral>(1042, "ko"),       // Korean
            std::pair<int, StringLiteral>(1045, "pl"),       // Polish
            std::pair<int, StringLiteral>(1046, "pt-BR"),    // Portuguese (Brazil)
            std::pair<int, StringLiteral>(1049, "ru"),       // Russian
            std::pair<int, StringLiteral>(1055, "tr"),       // Turkish
            std::pair<int, StringLiteral>(2052, "zh-Hans"),  // Chinese (Simplified)
            std::pair<int, StringLiteral>(1028, "zh-Hant")}; // Chinese (Traditional)

        for (auto&& l : languages)
        {
            if (l.first == LCID)
            {
                return l.second;
            }
        }

        return nullopt;
    }

    LocalizedString format_error(const LocalizedString& s) { return error_prefix().append(s); }
    void println_error(const LocalizedString& s) { println(Color::error, format_error(s)); }
    LocalizedString format_warning(const LocalizedString& s) { return warning_prefix().append(s); }
    void println_warning(const LocalizedString& s) { println(Color::warning, format_warning(s)); }
}
