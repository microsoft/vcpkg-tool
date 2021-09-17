#pragma once

#include <vcpkg/base/strings.h>
#include <vcpkg/base/view.h>

namespace vcpkg
{
#if defined(_WIN32)
    enum class Color : unsigned short
    {
        None = 0,
        Success = 0x0A, // FOREGROUND_GREEN | FOREGROUND_INTENSITY
        Error = 0xC,    // FOREGROUND_RED | FOREGROUND_INTENSITY
        Warning = 0xE,  // FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY
    };
#else
    enum class Color : char
    {
        None = 0,
        Success = '2', // [with 9] bright green
        Error = '1',   // [with 9] bright red
        Warning = '3', // [with 9] bright yellow
    };
#endif

    void write_text_to_stdout(Color c, StringView sv);

    template<class Arg1, class... Args>
    void printf(const char* message_template, const Arg1& message_arg1, const Args&... message_args)
    {
        return write_text_to_stdout(Color::None, Strings::format(message_template, message_arg1, message_args...));
    }

    template<class Arg1, class... Args>
    void printf(Color c, const char* message_template, const Arg1& message_arg1, const Args&... message_args)
    {
        return write_text_to_stdout(c, Strings::format(message_template, message_arg1, message_args...));
    }

    template<class... Args>
    void print2(Color c, const Args&... args)
    {
        write_text_to_stdout(c, Strings::concat_or_view(args...));
    }

    template<class... Args>
    void print2(const Args&... args)
    {
        write_text_to_stdout(Color::None, Strings::concat_or_view(args...));
    }

    struct BufferedPrint
    {
        BufferedPrint() { stdout_buffer.reserve(alloc_size); }
        BufferedPrint(const BufferedPrint&) = delete;
        BufferedPrint& operator=(const BufferedPrint&) = delete;
        void append(::vcpkg::StringView nextView)
        {
            stdout_buffer.append(nextView.data(), nextView.size());
            if (stdout_buffer.size() > buffer_size_target)
            {
                write_text_to_stdout(Color::None, stdout_buffer);
                stdout_buffer.clear();
            }
        }

        ~BufferedPrint() { write_text_to_stdout(Color::None, stdout_buffer); }

    private:
        ::std::string stdout_buffer;
        static constexpr ::std::size_t buffer_size_target = 2048;
        static constexpr ::std::size_t expected_maximum_print = 256;
        static constexpr ::std::size_t alloc_size = buffer_size_target + expected_maximum_print;
    };
}
