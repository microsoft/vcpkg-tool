#pragma once

#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/stringview.h>

namespace vcpkg::Checks
{
    // Additional convenience overloads on top of basic_checks.h that do formatting.

    // This function is a link seam called by final_cleanup_and_exit.
    void on_final_cleanup_and_exit();

    [[noreturn]] void log_final_cleanup_and_exit(const LineInfo& line_info, const int exit_code);
    [[noreturn]] void final_cleanup_and_exit(const int exit_code);

    // Indicate that an internal error has occurred and exit the tool. This should be used when invariants have been
    // broken.
    [[noreturn]] void unreachable(const LineInfo& line_info);
    [[noreturn]] void unreachable(const LineInfo& line_info, StringView message);

    [[noreturn]] void exit_with_code(const LineInfo& line_info, const int exit_code);

    // Exit the tool without an error message.
    [[noreturn]] void exit_fail(const LineInfo& line_info);

    // Exit the tool successfully.
    [[noreturn]] void exit_success(const LineInfo& line_info);

    // Display an error message to the user and exit the tool.
    [[noreturn]] void exit_with_message(const LineInfo& line_info, StringView error_message);
    [[noreturn]] void exit_with_message(const LineInfo& line_info, const LocalizedString&) = delete;
    [[noreturn]] void msg_exit_with_message(const LineInfo& line_info, const LocalizedString& error_message);
    template<VCPKG_DECL_MSG_TEMPLATE>
    [[noreturn]] void msg_exit_with_message(const LineInfo& line_info, VCPKG_DECL_MSG_ARGS)
    {
        msg_exit_with_message(line_info, msg::format(VCPKG_EXPAND_MSG_ARGS));
    }

    // If expression is false, call exit_fail.
    void check_exit(const LineInfo& line_info, bool expression);

    // if expression is false, call exit_with_message.
    void check_exit(const LineInfo& line_info, bool expression, StringView error_message);
    void check_exit(const LineInfo& line_info, bool expression, const LocalizedString&) = delete;
    void msg_check_exit(const LineInfo& line_info, bool expression, const LocalizedString& error_message);
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

    // Display a message indicating that vcpkg should be upgraded and exit.
    [[noreturn]] void exit_maybe_upgrade(const LineInfo& line_info);
    [[noreturn]] void exit_maybe_upgrade(const LineInfo& line_info, StringView error_message);
    [[noreturn]] void exit_maybe_upgrade(const LineInfo&, const LocalizedString&) = delete;
    [[noreturn]] void msg_exit_maybe_upgrade(const LineInfo& line_info, const LocalizedString& error_message);
    template<VCPKG_DECL_MSG_TEMPLATE>
    [[noreturn]] void msg_exit_maybe_upgrade(const LineInfo& line_info, VCPKG_DECL_MSG_ARGS)
    {
        msg_exit_maybe_upgrade(line_info, msg::format(VCPKG_EXPAND_MSG_ARGS));
    }

    // Check the indicated condition and call exit_maybe_upgrade if it is false.
    void check_maybe_upgrade(const LineInfo& line_info, bool condition);
    void check_maybe_upgrade(const LineInfo& line_info, bool condition, StringView error_message);
    void check_maybe_upgrade(const LineInfo& line_info, bool condition, const LocalizedString&) = delete;
    void msg_check_maybe_upgrade(const LineInfo& line_info, bool condition, const LocalizedString& error_message);
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

    [[noreturn]] inline void msg_exit_with_error(const LineInfo& line_info, const LocalizedString& message)
    {
        msg::write_unlocalized_text_to_stderr(Color::error, error_prefix().append_raw(message).append_raw('\n'));
        Checks::exit_fail(line_info);
    }
    template<VCPKG_DECL_MSG_TEMPLATE>
    [[noreturn]] void msg_exit_with_error(const LineInfo& line_info, VCPKG_DECL_MSG_ARGS)
    {
        msg::write_unlocalized_text_to_stderr(Color::error,
                                              error_prefix().append(VCPKG_EXPAND_MSG_ARGS).append_raw('\n'));
        Checks::exit_fail(line_info);
    }

}
