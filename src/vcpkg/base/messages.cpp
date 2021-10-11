#include <vcpkg/base/messages.h>

#include <vcpkg/base/json.h>

namespace vcpkg::msg
{
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
            ::fwprintf(stderr,
                L"[DEBUG] Failed to write to stdout: %lu\n",
                GetLastError()
            );
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
                ::fprintf(stderr,
                    "[DEBUG] Failed to print to stdout: %d\n",
                    errno
                );
                std::abort();
            }
            ptr += written;
            to_write -= written;
        }
    }

    void write_text_to_stdout(Color c, StringView sv)
    {
        static constexpr char reset_color_sequence[] = { '\033', '[', '0', 'm' };

        if (sv.empty()) return;

        bool reset_color = false;
        if (::isatty(STDOUT_FILENO) && c != Color::None)
        {
            reset_color = true;

            const char set_color_sequence[] = { '\033', '[', '9', static_cast<char>(c), 'm' };
            write_all(STDOUT_FILENO, set_color_sequence, sizeof(set_color_sequence));
        }

        write_all(STDOUT_FILENO, sv.data(), sv.size());

        if (reset_color)
        {
            write_all(STDOUT_FILENO, reset_color_sequence, sizeof(reset_color_sequence));
        }
    }
#endif

    // this is basically a SoA - each index is:
    // {
    //   name
    //   default_string
    //   localized_string
    // }
    // requires: names.size() == default_strings.size() == localized_strings.size()
    static std::vector<std::string> message_names; // const after startup
    static std::vector<std::string> default_message_strings; // const after startup
    static std::vector<std::string> localization_comments; // const after startup

    static std::atomic<bool> initialized;
    static std::vector<std::string> localized_strings; // const after initialization

    void threadunsafe_initialize_context()
    {
        if (initialized)
        {
            write_text_to_stdout(Color::Error, "double-initialized message context; this is a very serious bug in vcpkg\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        localized_strings.resize(message_names.size());
        initialized = true;
    }
    void threadunsafe_initialize_context(const Json::Object& message_map)
    {
        threadunsafe_initialize_context();
        std::vector<std::string> names_without_localization;

        for (::size_t index = 0; index < message_names.size(); ++index)
        {
            const auto& name = message_names[index];
            if (auto p = message_map.get(message_names[index]))
            {
                localized_strings[index] = p->string().to_string();
            }
            else
            {
                names_without_localization.push_back(name);
            }
        }

        if (!names_without_localization.empty())
        {
            println(Color::Warning, NoLocalizationForMessages);
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
            write_text_to_stdout(Color::Error, "Invalid locale file '{}' - locale file must be an object.\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        threadunsafe_initialize_context(message_map.first.object());
    }

    ::size_t detail::last_message_index()
    {
        return message_names.size();
    }

    static ::size_t startup_register_message(StringView name, StringView format_string, StringView comment)
    {
        auto res = message_names.size();
        message_names.push_back(name.to_string());
        default_message_strings.push_back(format_string.to_string());
        localization_comments.push_back(comment.to_string());
        return res;
    }

    StringView detail::get_format_string(::size_t index)
    {
        Checks::check_exit(VCPKG_LINE_INFO, localized_strings.size() == default_message_strings.size());
        Checks::check_exit(VCPKG_LINE_INFO, index < default_message_strings.size());
        const auto& localized = localized_strings[index];
        if(localized.empty())
        {
            return default_message_strings[index];
        }
        else
        {
            return localized;
        }
    }
    StringView detail::get_message_name(::size_t index)
    {
        Checks::check_exit(VCPKG_LINE_INFO, index < message_names.size());
        return message_names[index];
    }
    StringView detail::get_default_format_string(::size_t index)
    {
        Checks::check_exit(VCPKG_LINE_INFO, index < default_message_strings.size());
        return default_message_strings[index];
    }
    StringView detail::get_localization_comment(::size_t index)
    {
        Checks::check_exit(VCPKG_LINE_INFO, index < localization_comments.size());
        return localization_comments[index];
    }

#define REGISTER_MESSAGE(NAME) \
    const ::size_t NAME ## _t :: index = \
        startup_register_message(name(), default_format_string(), NAME ## _t::localization_comment())

    REGISTER_MESSAGE(VcpkgHasCrashed);
    REGISTER_MESSAGE(AllRequestedPackagesInstalled);
    REGISTER_MESSAGE(NoLocalizationForMessages);
    REGISTER_MESSAGE(UnreachableCode);
    REGISTER_MESSAGE(FailedToStoreBinaryCache);
    REGISTER_MESSAGE(UsingCommunityTriplet);
    REGISTER_MESSAGE(VersionAlreadyInBaseline);
    REGISTER_MESSAGE(VersionAddedToBaseline);
    REGISTER_MESSAGE(NoLocalGitShaFoundForPort);
    REGISTER_MESSAGE(PortNotProperlyFormatted);
    REGISTER_MESSAGE(CouldntLoadPort);
    REGISTER_MESSAGE(AddVersionUseOptionAll);
    REGISTER_MESSAGE(AddVersionIgnoringOptionAll);

#undef REGISTER_MESSAGE

}
