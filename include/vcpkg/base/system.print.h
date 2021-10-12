#pragma once

#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/view.h>

namespace vcpkg
{
    template<class Arg1, class... Args>
    void printf(const char* message_template, const Arg1& message_arg1, const Args&... message_args)
    {
        return msg::write_unlocalized_text_to_stdout(Color::None, Strings::format(message_template, message_arg1, message_args...));
    }

    template<class Arg1, class... Args>
    void printf(Color c, const char* message_template, const Arg1& message_arg1, const Args&... message_args)
    {
        return msg::write_unlocalized_text_to_stdout(c, Strings::format(message_template, message_arg1, message_args...));
    }

    template<class... Args>
    void print2(Color c, const Args&... args)
    {
        msg::write_unlocalized_text_to_stdout(c, Strings::concat_or_view(args...));
    }

    template<class... Args>
    void print2(const Args&... args)
    {
        msg::write_unlocalized_text_to_stdout(Color::None, Strings::concat_or_view(args...));
    }

}
