#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>

namespace vcpkg::msg
{
    DECLARE_AND_REGISTER_MESSAGE(NoLocalizationForMessages, (), "", "No localization for the following messages:");

    // basic implementation - the write_unlocalized_text_to_stdout
#if defined(_WIN32)
    static bool is_console(HANDLE h)
    {
        DWORD mode = 0;
        // GetConsoleMode succeeds iff `h` is a console
        // we do not actually care about the mode of the console
        return GetConsoleMode(h, &mode);
    }

    static void check_stdout_operation(BOOL success, const wchar_t* err)
    {
        if (!success)
        {
            ::fwprintf(stderr, L"[DEBUG] Failed to %ls: %lu\n", err, GetLastError());
            ::abort();
        }
    }
    static DWORD size_to_write(::size_t size)
    {
        return size > static_cast<DWORD>(-1) // DWORD_MAX
                   ? static_cast<DWORD>(-1)
                   : static_cast<DWORD>(size);
    }

    void write_unlocalized_text_to_stdout(Color c, StringView sv)
    {
        if (sv.empty()) return;

        static const HANDLE stdout_handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
        static const bool stdout_is_console = is_console(stdout_handle);

        if (stdout_is_console)
        {
            WORD original_color = 0;
            if (c != Color::none)
            {
                CONSOLE_SCREEN_BUFFER_INFO console_screen_buffer_info{};
                check_stdout_operation(
                    ::GetConsoleScreenBufferInfo(stdout_handle, &console_screen_buffer_info),
                    L"get screen buffer info");
                original_color = console_screen_buffer_info.wAttributes;
                
                check_stdout_operation(
                    ::SetConsoleTextAttribute(stdout_handle, static_cast<WORD>(c) | (original_color & 0xF0))
                    L"set text attribute");
            }

            auto as_wstr = Strings::to_utf16(sv);

            const wchar_t* pointer = as_wstr.data();
            ::size_t size = as_wstr.size();

            while (size != 0)
            {
                DWORD written;
                check_stdout_operation(
                    ::WriteConsoleW(stdout_handle, pointer, size_to_write(size), &written, nullptr) && written != 0,
                    L"write to stdout");
                pointer += written;
                size -= written;
            }

            if (c != Color::none)
            {
                check_stdout_operation(
                    ::SetConsoleTextAttribute(stdout_handle, original_color)
                    L"set text attribute back");
            }
        }
        else
        {
            const char* pointer = sv.data();
            ::size_t size = sv.size();

            while (size != 0)
            {
                DWORD written = 0;
                check_stdout_operation(
                    ::WriteFile(stdout_handle, pointer, size_to_write(size), &written, nullptr),
                    L"write to stdout as a file");
                pointer += written;
                size -= written;
            }
        }
    }
#else
    static void write_all(const char* ptr, size_t to_write)
    {
        while (to_write != 0)
        {
            auto written = ::write(STDOUT_FILENO, ptr, to_write);
            if (written == -1)
            {
                ::fprintf(stderr, "[DEBUG] Failed to print to stdout: %d\n", errno);
                std::abort();
            }
            ptr += written;
            to_write -= written;
        }
    }

    void write_unlocalized_text_to_stdout(Color c, StringView sv)
    {
        static constexpr char reset_color_sequence[] = {'\033', '[', '0', 'm'};

        if (sv.empty()) return;

        static bool is_a_tty = ::isatty(STDOUT_FILENO);

        bool reset_color = false;
        if (is_a_tty && c != Color::none)
        {
            reset_color = true;

            const char set_color_sequence[] = {'\033', '[', '9', static_cast<char>(c), 'm'};
            write_all(set_color_sequence, sizeof(set_color_sequence));
        }

        write_all(sv.data(), sv.size());

        if (reset_color)
        {
            write_all(reset_color_sequence, sizeof(reset_color_sequence));
        }
    }
#endif

    namespace
    {
        struct Messages
        {
            // this is basically a SoA - each index is:
            // {
            //   name
            //   default_string
            //   localization_comment
            //   localized_string
            // }
            // requires: names.size() == default_strings.size() == localized_strings.size()
            std::vector<StringLiteral> names;
            std::vector<StringLiteral> default_strings;       // const after startup
            std::vector<StringLiteral> localization_comments; // const after startup

            bool initialized = false;
            std::vector<std::string> localized_strings;
        };

        // to avoid static initialization order issues,
        // everything that needs the messages needs to get it from this function
        Messages& messages()
        {
            static Messages m;
            return m;
        }
    }

    LocalizedString& append_newline(LocalizedString& s)
    {
        s.m_data.push_back('\n');
        return s;
    }

    void threadunsafe_initialize_context()
    {
        Messages& m = messages();
        if (m.initialized)
        {
            write_unlocalized_text_to_stdout(
                Color::error, "double-initialized message context; this is a very serious bug in vcpkg\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        m.localized_strings.resize(m.names.size());
        m.initialized = true;
    }
    void threadunsafe_initialize_context(const Json::Object& message_map)
    {
        threadunsafe_initialize_context();

        Messages& m = messages();
        std::vector<std::string> names_without_localization;

        for (::size_t index = 0; index < m.names.size(); ++index)
        {
            const auto& name = m.names[index];
            if (auto p = message_map.get(m.names[index]))
            {
                m.localized_strings[index] = p->string().to_string();
            }
            else if (Debug::g_debugging)
            {
                // we only want to print these in debug
                names_without_localization.push_back(name);
            }
        }

        if (!names_without_localization.empty())
        {
            println(Color::warning, msgNoLocalizationForMessages);
            for (const auto& name : names_without_localization)
            {
                write_unlocalized_text_to_stdout(Color::warning, fmt::format("    - {}\n", name));
            }
        }
    }

    void threadunsafe_initialize_context(const Filesystem& fs, StringView language, const Path& locale_base)
    {
        auto path_to_locale = locale_base;
        path_to_locale /= language;
        path_to_locale += ".json";

        auto message_map = Json::parse_file(VCPKG_LINE_INFO, fs, path_to_locale);
        if (!message_map.first.is_object())
        {
            write_unlocalized_text_to_stdout(
                Color::error,
                fmt::format("Invalid locale file '{}' - locale file must be an object.\n", path_to_locale));
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        threadunsafe_initialize_context(message_map.first.object());
    }

    ::size_t detail::number_of_messages() { return messages().names.size(); }

    ::size_t detail::startup_register_message(StringLiteral name, StringLiteral format_string, StringLiteral comment)
    {
        Messages& m = messages();

        auto name_is_used = std::find(m.names.begin(), m.names.end(), name);
        if (name_is_used != m.names.end())
        {
            write_unlocalized_text_to_stdout(
                Color::error,
                fmt::format("INTERNAL ERROR: localization message '{}' has been declared multiple times\n", name));
            write_unlocalized_text_to_stdout(Color::error, fmt::format("INTERNAL ERROR: first message:\n"));
            write_unlocalized_text_to_stdout(Color::none, m.default_strings[name_is_used - m.names.begin()]);
            write_unlocalized_text_to_stdout(Color::error, fmt::format("\nINTERNAL ERROR: second message:\n"));
            write_unlocalized_text_to_stdout(Color::none, format_string);
            write_unlocalized_text_to_stdout(Color::none, "\n");
            ::abort();
        }

        auto res = m.names.size();
        m.names.push_back(name);
        m.default_strings.push_back(format_string);
        m.localization_comments.push_back(comment);
        return res;
    }

    StringView detail::get_format_string(::size_t index)
    {
        Messages& m = messages();
        // these use this instead of Checks::check_exit to avoid infinite recursion
        if (m.localized_strings.size() != m.default_strings.size())
        {
            write_unlocalized_text_to_stdout(
                Color::error,
                fmt::format("Internal error: localized_strings.size() ({}) != default_strings.size() ({})\n",
                            m.localized_strings.size(),
                            m.default_strings.size()));
            std::abort();
        }
        if (index >= m.default_strings.size())
        {
            write_unlocalized_text_to_stdout(Color::error,
                                             fmt::format("Internal error: index ({}) >= default_strings.size() ({})\n",
                                                         index,
                                                         m.default_strings.size()));
            std::abort();
        }
        const auto& localized = m.localized_strings[index];
        if (localized.empty())
        {
            return m.default_strings[index];
        }

        return localized;
    }
    StringView detail::get_message_name(::size_t index)
    {
        Messages& m = messages();
        Checks::check_exit(VCPKG_LINE_INFO, index < m.names.size());
        return m.names[index];
    }
    StringView detail::get_default_format_string(::size_t index)
    {
        Messages& m = messages();
        Checks::check_exit(VCPKG_LINE_INFO, index < m.default_strings.size());
        return m.default_strings[index];
    }
    StringView detail::get_localization_comment(::size_t index)
    {
        Messages& m = messages();
        Checks::check_exit(VCPKG_LINE_INFO, index < m.localization_comments.size());
        return m.localization_comments[index];
    }

    LocalizedString detail::internal_vformat(::size_t index, fmt::format_args args)
    {
        auto fmt_string = get_format_string(index);
        return LocalizedString::from_string_unchecked(fmt::vformat({fmt_string.data(), fmt_string.size()}, args));
    }

}
