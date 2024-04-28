#pragma once

#include <vcpkg/base/fwd/messages.h>
#include <vcpkg/base/fwd/span.h>
#include <vcpkg/base/fwd/system.console.h>

#include <vcpkg/base/stringview.h>

#include <mutex>

namespace vcpkg
{
    class Console
    {
    public:
#ifdef _WIN32
        explicit Console(unsigned long std_device) noexcept;
#else
        explicit Console(int fd) noexcept;
#endif
        ~Console() = default;

        // This function is safe to call from multiple threads.
        // When called from multiple threads, calls are atomic with respect to other callers
        void print(Color color, StringView text)
        {
            std::lock_guard<std::mutex> lck{mtx};
            print_unlocked(color, text);
            flush();
        }

        // This function is safe to call from multiple threads.
        // When called from multiple threads, calls are atomic with respect to other callers
        void print_lines(View<std::pair<Color, StringView>> lines);

        // This function is safe to call from multiple threads.
        // When called from multiple threads, calls are atomic with respect to other callers
        void println(Color color, StringView text)
        {
            std::lock_guard<std::mutex> lck{mtx};
            print_unlocked(color, text);
            print_unlocked(Color::none, "\n");
            flush();
        }

        // This function is safe to call from multiple threads.
        // When called from multiple threads, calls are atomic with respect to other callers
        void println(Color color, std::string&& text)
        {
            text.push_back('\n');
            print(color, text);
        }

    private:
        void print_unlocked(Color color, StringView text) noexcept;
        void write(const char* text, size_t count) noexcept;
        void flush() noexcept;
        bool check_is_terminal() noexcept;

        std::mutex mtx;
#if _WIN32
        void* fd;
#else
        int fd;
#endif
        bool is_terminal;
    };
} // namespace vcpkg
