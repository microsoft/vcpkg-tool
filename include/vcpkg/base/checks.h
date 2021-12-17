#pragma once

#include <vcpkg/base/basic_checks.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>

namespace vcpkg::Checks
{
    // Additional convenience overloads on top of basic_checks.h that do formatting.

    template<class Arg1, class... Args>
    // Display an error message to the user and exit the tool.
    [[noreturn]] void exit_with_message(const LineInfo& line_info,
                                        const char* error_message_template,
                                        const Arg1& error_message_arg1,
                                        const Args&... error_message_args)
    {
        exit_with_message(line_info,
                          Strings::format(error_message_template, error_message_arg1, error_message_args...));
    }
    template<class Message, class... Args>
    [[noreturn]] typename Message::is_message_type exit_with_message(const LineInfo& line_info,
                                                                     Message m,
                                                                     const Args&... args)
    {
        exit_with_message(line_info, msg::format(m, args...));
    }

    template<class Conditional, class Arg1, class... Args>
    void check_exit(const LineInfo& line_info,
                    const Conditional& expression,
                    const char* error_message_template,
                    const Arg1& error_message_arg1,
                    const Args&... error_message_args)
    {
        if (!expression)
        {
            // Only create the string if the expression is false
            exit_with_message(line_info,
                              Strings::format(error_message_template, error_message_arg1, error_message_args...));
        }
    }
    template<class Conditional, class Message, class... Args>
    typename Message::is_message_type check_exit(const LineInfo& line_info,
                                                 const Conditional& expression,
                                                 Message m,
                                                 const Args&... args)
    {
        if (!expression)
        {
            // Only create the string if the expression is false
            exit_with_message(line_info, msg::format(m, args...));
        }
    }

    template<class Arg1, class... Args>
    [[noreturn]] void exit_maybe_upgrade(const LineInfo& line_info,
                                         const char* error_message_template,
                                         const Arg1& error_message_arg1,
                                         const Args&... error_message_args)
    {
        exit_maybe_upgrade(line_info,
                           Strings::format(error_message_template, error_message_arg1, error_message_args...));
    }
    template<class Message, class... Args>
    [[noreturn]] typename Message::is_message_type exit_maybe_upgrade(const LineInfo& line_info,
                                                                      Message m,
                                                                      const Args&... args)
    {
        exit_maybe_upgrade(line_info, msg::format(m, args...));
    }

    template<class Conditional, class Arg1, class... Args>
    void check_maybe_upgrade(const LineInfo& line_info,
                             const Conditional& expression,
                             const char* error_message_template,
                             const Arg1& error_message_arg1,
                             const Args&... error_message_args)
    {
        if (!expression)
        {
            // Only create the string if the expression is false
            exit_maybe_upgrade(line_info,
                               Strings::format(error_message_template, error_message_arg1, error_message_args...));
        }
    }
    template<class Conditional, class Message, class... Args>
    typename Message::is_message_type check_maybe_upgrade(const LineInfo& line_info,
                                                          const Conditional& expression,
                                                          Message m,
                                                          const Args&... args)
    {
        if (!expression)
        {
            // Only create the string if the expression is false
            exit_maybe_upgrade(line_info, msg::format(m, args...));
        }
    }
}
