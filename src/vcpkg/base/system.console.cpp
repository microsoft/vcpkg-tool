#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.console.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace vcpkg
{
#ifdef _WIN32
    Console::Console(unsigned long std_device) noexcept
        : fd(::GetStdHandle(std_device)), is_terminal(this->check_is_terminal())
    {
    }
#else
    Console::Console(int fd) noexcept : fd(fd), is_terminal(this->check_is_terminal()) { }
#endif

    void Console::flush() noexcept
    {
#ifdef _WIN32
        ::FlushFileBuffers(fd);
#else
        ::fflush(fd);
#endif
    }

    bool Console::check_is_terminal() noexcept
    {
#ifdef _WIN32
        DWORD mode = 0;
        // GetConsoleMode succeeds if `h` is a console
        // we do not actually care about the mode of the console
        return GetConsoleMode(fd, &mode);
#else
        return ::isatty(fd) == 1;
#endif
    }

    void Console::print_lines(View<std::pair<Color, StringView>> lines)
    {
        std::lock_guard<std::mutex> lck{mtx};

        for (auto&& line : lines)
        {
            print_unlocked(line.first, line.second);

            if (line.second.back() != '\n')
            {
                print_unlocked(Color::none, "\n");
            }
        }
    }

#ifdef _WIN32
    void Console::print_unlocked(Color c, StringView sv) noexcept
    {
        if (sv.empty()) return;

        WORD original_color = 0;

        if (is_terminal && c != Color::none)
        {
            CONSOLE_SCREEN_BUFFER_INFO console_screen_buffer_info{};
            ::GetConsoleScreenBufferInfo(fd, &console_screen_buffer_info);
            original_color = console_screen_buffer_info.wAttributes;
            ::SetConsoleTextAttribute(fd, static_cast<WORD>(c) | (original_color & 0xF0));
        }

        write(sv.data(), sv.size());

        if (is_terminal && c != Color::none)
        {
            ::SetConsoleTextAttribute(fd, original_color);
        }
    }

    static constexpr DWORD size_to_write(size_t size) noexcept
    {
        return size > MAXDWORD ? MAXDWORD : static_cast<DWORD>(size);
    }

    void Console::write(const char* text, size_t count) noexcept
    {
        while (count != 0)
        {
            DWORD written = 0;
            if (::WriteFile(fd, text, size_to_write(count), &written, nullptr))
            {
                ::fwprintf(stderr, L"[DEBUG] Failed to write to stdout: %lu\n", GetLastError());
                std::abort();
            }
            text += written;
            count -= written;
        }
    }

#else

    void Console::print_unlocked(Color c, StringView sv) noexcept
    {
        static constexpr char reset_color_sequence[] = {'\033', '[', '0', 'm'};

        if (sv.empty()) return;

        bool reset_color = false;
        // Only write color sequence if file descriptor is a terminal
        if (is_terminal && c != Color::none)
        {
            reset_color = true;

            const char set_color_sequence[] = {'\033', '[', '9', static_cast<char>(c), 'm'};
            write(set_color_sequence, sizeof(set_color_sequence));
        }

        write(sv.data(), sv.size());

        if (reset_color)
        {
            write(reset_color_sequence, sizeof(reset_color_sequence));
        }
    }

    void Console::write(const char* text, size_t count) noexcept
    {
        while (count != 0)
        {
            auto written = ::write(fd, text, count);
            if (written == -1)
            {
                ::fprintf(stderr, "[DEBUG] Failed to print to stdout: %d\n", errno);
                std::abort();
            }
            text += written;
            count -= written;
        }
    }
#endif
#ifdef _WIN32
    Console std_out_instance(STD_OUTPUT_HANDLE);
    Console std_error_instance(STD_ERROR_HANDLE);
#else
    Console std_out_instance(STDOUT_FILENO);
    Console std_error_instance(STDERR_FILENO);
#endif

    Console& std_out = std_out_instance;
    Console& std_error = std_out_instance;
} // namespace vcpkg
