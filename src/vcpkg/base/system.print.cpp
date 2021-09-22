#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

namespace vcpkg
{
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
}
