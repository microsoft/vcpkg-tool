#pragma once

#include <vcpkg/base/basic-checks.h>
#include <vcpkg/base/messages.h>

namespace vcpkg::Checks
{
    // Additional convenience overloads on top of basic_checks.h that do formatting.
    template<VCPKG_DECL_MSG_TEMPLATE>
    [[noreturn]] void msg_exit_with_message(const LineInfo& line_info, VCPKG_DECL_MSG_ARGS)
    {
        msg_exit_with_message(line_info, msg::format(VCPKG_EXPAND_MSG_ARGS));
    }

    [[noreturn]] inline void msg_exit_with_error(const LineInfo& line_info, const LocalizedString& message)
    {
        msg_exit_with_message(line_info, msg::format(msgErrorMessage).append(message));
    }

    template<VCPKG_DECL_MSG_TEMPLATE>
    [[noreturn]] void msg_exit_with_error(const LineInfo& line_info, VCPKG_DECL_MSG_ARGS)
    {
        msg_exit_with_message(line_info, msg::format(msgErrorMessage).append(VCPKG_EXPAND_MSG_ARGS));
    }
    template<VCPKG_DECL_MSG_TEMPLATE>
    VCPKG_SAL_ANNOTATION(_Post_satisfies_(_Old_(expression)))
    void msg_check_exit(const LineInfo& line_info, bool expression, VCPKG_DECL_MSG_ARGS)
    {
        if (!expression)
        {
            // Only create the string if the expression is false
            msg_exit_with_message(line_info, msg::format(VCPKG_EXPAND_MSG_ARGS));
        }
    }

    template<VCPKG_DECL_MSG_TEMPLATE>
    [[noreturn]] void msg_exit_maybe_upgrade(const LineInfo& line_info, VCPKG_DECL_MSG_ARGS)
    {
        msg_exit_maybe_upgrade(line_info, msg::format(VCPKG_EXPAND_MSG_ARGS));
    }
    template<VCPKG_DECL_MSG_TEMPLATE>
    VCPKG_SAL_ANNOTATION(_Post_satisfies_(_Old_(expression)))
    void msg_check_maybe_upgrade(const LineInfo& line_info, bool expression, VCPKG_DECL_MSG_ARGS)
    {
        if (!expression)
        {
            // Only create the string if the expression is false
            msg_exit_maybe_upgrade(line_info, msg::format(VCPKG_EXPAND_MSG_ARGS));
        }
    }
}
