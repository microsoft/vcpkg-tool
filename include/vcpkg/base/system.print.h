#pragma once

#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/base/span.h>
#include <vcpkg/base/strings.h>

namespace vcpkg
{
    namespace details
    {
        void print(StringView message);
        void print(const Color c, StringView message);
    }

    template<class Arg1, class... Args>
    void printf(const char* message_template, const Arg1& message_arg1, const Args&... message_args)
    {
        return ::vcpkg::details::print(Strings::format(message_template, message_arg1, message_args...));
    }

    template<class Arg1, class... Args>
    void printf(const Color c, const char* message_template, const Arg1& message_arg1, const Args&... message_args)
    {
        return ::vcpkg::details::print(c, Strings::format(message_template, message_arg1, message_args...));
    }
}
