#pragma once

#include <vcpkg/base/basic-checks.h>
#include <vcpkg/base/messages.h>

namespace vcpkg::Checks
{
    // Additional convenience overloads on top of basic_checks.h that do formatting.
    template<class Message, class... Args>
    [[noreturn]] typename Message::is_message_type msg_exit_with_message(const LineInfo& line_info,
                                                                         Message m,
                                                                         const Args&... args)
    {
        msg_exit_with_message(line_info, msg::format(m, args...));
    }

    [[noreturn]] inline void msg_exit_with_error(const LineInfo& line_info, const LocalizedString& message)
    {
        msg_exit_with_message(line_info, msg::format(msg::msgErrorMessage).append(message));
    }

    template<class Message, class... Args>
    [[noreturn]] typename Message::is_message_type msg_exit_with_error(const LineInfo& line_info,
                                                                       Message m,
                                                                       const Args&... args)
    {
        msg_exit_with_message(line_info, msg::format(msg::msgErrorMessage).append(msg::format(m, args...)));
    }
    template<class Message, class... Args>
    VCPKG_SAL_ANNOTATION(_Post_satisfies_(_Old_(expression)))
    typename Message::is_message_type
        msg_check_exit(const LineInfo& line_info, bool expression, Message m, const Args&... args)
    {
        if (!expression)
        {
            // Only create the string if the expression is false
            msg_exit_with_message(line_info, msg::format(m, args...));
        }
    }
    template<class Message, class... Args>
    [[noreturn]] typename Message::is_message_type msg_exit_maybe_upgrade(const LineInfo& line_info,
                                                                          Message m,
                                                                          const Args&... args)
    {
        msg_exit_maybe_upgrade(line_info, msg::format(m, args...));
    }
    template<class Message, class... Args>
    VCPKG_SAL_ANNOTATION(_Post_satisfies_(_Old_(expression)))
    typename Message::is_message_type
        msg_check_maybe_upgrade(const LineInfo& line_info, bool expression, Message m, const Args&... args)
    {
        if (!expression)
        {
            // Only create the string if the expression is false
            msg_exit_maybe_upgrade(line_info, msg::format(m, args...));
        }
    }
}
