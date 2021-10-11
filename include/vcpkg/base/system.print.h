#pragma once

#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/view.h>

namespace vcpkg
{
    template<class Arg1, class... Args>
    void printf(const char* message_template, const Arg1& message_arg1, const Args&... message_args)
    {
        return msg::write_text_to_stdout(Color::None, Strings::format(message_template, message_arg1, message_args...));
    }

    template<class Arg1, class... Args>
    void printf(Color c, const char* message_template, const Arg1& message_arg1, const Args&... message_args)
    {
        return msg::write_text_to_stdout(c, Strings::format(message_template, message_arg1, message_args...));
    }

    template<class... Args>
    void print2(Color c, const Args&... args)
    {
        msg::write_text_to_stdout(c, Strings::concat_or_view(args...));
    }

    template<class... Args>
    void print2(const Args&... args)
    {
        msg::write_text_to_stdout(Color::None, Strings::concat_or_view(args...));
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
                msg::write_text_to_stdout(Color::None, stdout_buffer);
                stdout_buffer.clear();
            }
        }

        ~BufferedPrint() { msg::write_text_to_stdout(Color::None, stdout_buffer); }

    private:
        ::std::string stdout_buffer;
        static constexpr ::std::size_t buffer_size_target = 2048;
        static constexpr ::std::size_t expected_maximum_print = 256;
        static constexpr ::std::size_t alloc_size = buffer_size_target + expected_maximum_print;
    };
}
