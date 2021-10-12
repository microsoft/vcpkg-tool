#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>

namespace vcpkg::msg
{
    DECLARE_AND_REGISTER_MESSAGE(NoLocalizationForMessages, (), "", "No localization for the following messages:");

    // basic implementation - the write_text_to_stdout
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
    static DWORD size_to_write(::size_t size)
    {
        return size > static_cast<DWORD>(-1) // DWORD_MAX
                   ? static_cast<DWORD>(-1)
                   : static_cast<DWORD>(size);
    }

    void write_text_to_stdout(Color c, StringView sv)
    {
        if (sv.empty()) return;

        auto handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (is_console(handle))
        {
            WORD original_color = 0;
            if (c != Color::None)
            {
                CONSOLE_SCREEN_BUFFER_INFO console_screen_buffer_info{};
                ::GetConsoleScreenBufferInfo(handle, &console_screen_buffer_info);
                original_color = console_screen_buffer_info.wAttributes;
                ::SetConsoleTextAttribute(handle, static_cast<WORD>(c) | (original_color & 0xF0));
            }

            auto as_wstr = Strings::to_utf16(sv);

            const wchar_t* pointer = as_wstr.data();
            ::size_t size = as_wstr.size();

            while (size != 0)
            {
                DWORD written = 0;
                check_write(::WriteConsoleW(handle, pointer, size_to_write(size), &written, nullptr));
                pointer += written;
                size -= written;
            }

            if (c != Color::None)
            {
                ::SetConsoleTextAttribute(handle, original_color);
            }
        }
        else
        {
            const char* pointer = sv.data();
            ::size_t size = sv.size();

            while (size != 0)
            {
                DWORD written = 0;
                check_write(::WriteFile(handle, pointer, size_to_write(size), &written, nullptr));
                pointer += written;
                size -= written;
            }
        }
    }
#else
    static void write_all(int fd, const char* ptr, size_t to_write)
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

    void write_text_to_stdout(Color c, StringView sv)
    {
        static constexpr char reset_color_sequence[] = {'\033', '[', '0', 'm'};

        if (sv.empty()) return;

        bool reset_color = false;
        if (::isatty(STDOUT_FILENO) && c != Color::None)
        {
            reset_color = true;

            const char set_color_sequence[] = {'\033', '[', '9', static_cast<char>(c), 'm'};
            write_all(STDOUT_FILENO, set_color_sequence, sizeof(set_color_sequence));
        }

        write_all(STDOUT_FILENO, sv.data(), sv.size());

        if (reset_color)
        {
            write_all(STDOUT_FILENO, reset_color_sequence, sizeof(reset_color_sequence));
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

    void threadunsafe_initialize_context()
    {
        Messages& m = messages();
        if (m.initialized)
        {
            write_text_to_stdout(Color::Error,
                                 "double-initialized message context; this is a very serious bug in vcpkg\n");
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
            else
            {
                names_without_localization.push_back(name);
            }
        }

        if (!names_without_localization.empty())
        {
            println(Color::Warning, msgNoLocalizationForMessages);
            for (const auto& name : names_without_localization)
            {
                write_text_to_stdout(Color::Warning, fmt::format("    - {}\n", name));
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
            write_text_to_stdout(
                Color::Error,
                fmt::format("Invalid locale file '{}' - locale file must be an object.\n", path_to_locale));
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        threadunsafe_initialize_context(message_map.first.object());
    }

    ::size_t detail::number_of_messages() { return messages().names.size(); }

    ::size_t detail::startup_register_message(StringLiteral name, StringLiteral format_string, StringLiteral comment)
    {
        Messages& m = messages();
        auto res = m.names.size();
        m.names.push_back(name);
        m.default_strings.push_back(format_string);
        m.localization_comments.push_back(comment);
        return res;
    }

    StringView detail::get_format_string(::size_t index)
    {
        Messages& m = messages();
        Checks::check_exit(VCPKG_LINE_INFO, m.localized_strings.size() == m.default_strings.size());
        Checks::check_exit(VCPKG_LINE_INFO, index < m.default_strings.size());
        const auto& localized = m.localized_strings[index];
        if (localized.empty())
        {
            return m.default_strings[index];
        }
        else
        {
            return localized;
        }
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
}
