#pragma once

#include <vcpkg/base/fwd/messages.h>
#include <vcpkg/base/fwd/span.h>
#include <vcpkg/base/fwd/system.console.h>

#include <vcpkg/base/stringview.h>

#include <mutex>

#include <unistd.h>

namespace vcpkg
{
    class Console
    {
    public:
        explicit Console(int fd) noexcept : fd(fd), is_terminal(::isatty(fd) == 1) {}
        ~Console() = default;

        void print(Color color, StringView text)
        {
            std::lock_guard<std::mutex> lck{mtx};
            print_unlocked(color, text);
        }

        void print_lines(View<std::pair<Color, StringView>> lines);
        void println(Color color, StringView text)
        {
            std::lock_guard<std::mutex> lck{mtx};
            print_unlocked(color, text);
            print_unlocked(Color::none, "\n");
        }
        void println(Color color, std::string&& text)
        {
            text.push_back('\n');
            print(color, text);
        }

    private:
        void print_unlocked(Color color, StringView text) noexcept;
        void write(const char* text, size_t count) noexcept;

        std::mutex mtx;
        int fd;
        bool is_terminal;
    };
} // namespace vcpkg

