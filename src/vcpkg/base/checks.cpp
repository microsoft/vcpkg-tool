#include <vcpkg/base/checks.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>

#include <vcpkg/metrics.h>

#include <stdlib.h>

namespace
{
    using namespace vcpkg;

    LocalizedString locale_invariant_lineinfo(const LineInfo& line_info)
    {
        return LocalizedString::from_raw(fmt::format("{}: ", line_info));
    }

}

std::string vcpkg::LineInfo::to_string() const { return fmt::format("{}({})", file_name, line_number); }

namespace vcpkg
{
    [[noreturn]] void Checks::log_final_cleanup_and_exit(const LineInfo& line_info, const int exit_code)
    {
        get_global_metrics_collector().track_string(StringMetric::ExitCode, std::to_string(exit_code));
        get_global_metrics_collector().track_string(
            StringMetric::ExitLocation,
            fmt::format(
                "{}:{}:{}", Path{line_info.file_name}.filename(), line_info.function_name, line_info.line_number));

        Checks::final_cleanup_and_exit(exit_code);
    }

    [[noreturn]] void Checks::final_cleanup_and_exit(const int exit_code)
    {
        static std::atomic<bool> have_entered{false};
        if (have_entered.exchange(true))
        {
#if defined(_WIN32)
            ::TerminateProcess(::GetCurrentProcess(), exit_code);
#else
            std::abort();
#endif
        }

        on_final_cleanup_and_exit();

        fflush(nullptr);

#if defined(_WIN32)
        ::TerminateProcess(::GetCurrentProcess(), exit_code);
#endif
        std::exit(exit_code);
    }

    [[noreturn]] void Checks::unreachable(const LineInfo& line_info)
    {
        msg::write_unlocalized_text_to_stderr(
            Color::error, locale_invariant_lineinfo(line_info).append(msgChecksUnreachableCode).append_raw('\n'));
#ifndef NDEBUG
        std::abort();
#else
        final_cleanup_and_exit(EXIT_FAILURE);
#endif
    }

    [[noreturn]] void Checks::unreachable(const LineInfo& line_info, StringView message)
    {
        msg::write_unlocalized_text_to_stderr(
            Color::error, locale_invariant_lineinfo(line_info).append_raw(message).append_raw('\n'));
#ifndef NDEBUG
        std::abort();
#else
        final_cleanup_and_exit(EXIT_FAILURE);
#endif
    }

    [[noreturn]] void Checks::exit_with_code(const LineInfo& line_info, const int exit_code)
    {
        Debug::println(locale_invariant_lineinfo(line_info));
        log_final_cleanup_and_exit(line_info, exit_code);
    }

    [[noreturn]] void Checks::exit_fail(const LineInfo& line_info) { exit_with_code(line_info, EXIT_FAILURE); }

    [[noreturn]] void Checks::exit_success(const LineInfo& line_info) { exit_with_code(line_info, EXIT_SUCCESS); }

    [[noreturn]] void Checks::exit_with_message(const LineInfo& line_info, StringView error_message)
    {
        msg::write_unlocalized_text(Color::error, error_message);
        msg::write_unlocalized_text(Color::error, "\n");
        exit_fail(line_info);
    }
    [[noreturn]] void Checks::msg_exit_with_message(const LineInfo& line_info, const LocalizedString& error_message)
    {
        msg::println(Color::error, error_message);
        exit_fail(line_info);
    }

    void Checks::check_exit(const LineInfo& line_info, bool expression)
    {
        if (!expression)
        {
            msg::write_unlocalized_text_to_stderr(Color::error,
                                                  internal_error_prefix()
                                                      .append(locale_invariant_lineinfo(line_info))
                                                      .append(msgChecksFailedCheck)
                                                      .append_raw('\n')
                                                      .append(msgInternalErrorMessageContact)
                                                      .append_raw('\n'));
            exit_fail(line_info);
        }
    }

    void Checks::check_exit(const LineInfo& line_info, bool expression, StringView error_message)
    {
        if (!expression)
        {
            msg::write_unlocalized_text_to_stderr(Color::error,
                                                  internal_error_prefix()
                                                      .append(locale_invariant_lineinfo(line_info))
                                                      .append_raw(error_message)
                                                      .append_raw('\n')
                                                      .append(msgInternalErrorMessageContact)
                                                      .append_raw('\n'));
            exit_fail(line_info);
        }
    }

    void Checks::msg_check_exit(const LineInfo& line_info, bool expression, const LocalizedString& error_message)
    {
        if (!expression)
        {
            msg_exit_with_message(line_info, error_message);
        }
    }

    static void display_upgrade_message()
    {
        msg::write_unlocalized_text_to_stderr(Color::error,
                                              note_prefix().append(msgChecksUpdateVcpkg).append_raw('\n'));
    }

    [[noreturn]] void Checks::exit_maybe_upgrade(const LineInfo& line_info)
    {
        display_upgrade_message();
        exit_fail(line_info);
    }

    [[noreturn]] void Checks::exit_maybe_upgrade(const LineInfo& line_info, StringView error_message)
    {
        msg::write_unlocalized_text_to_stderr(
            Color::error,
            LocalizedString::from_raw(error_message).append_raw('\n').append(msgChecksUpdateVcpkg).append_raw('\n'));
        exit_fail(line_info);
    }
    [[noreturn]] void Checks::msg_exit_maybe_upgrade(const LineInfo& line_info, const LocalizedString& error_message)
    {
        msg::write_unlocalized_text_to_stderr(Color::error, error_message);
        msg::write_unlocalized_text_to_stderr(Color::none, "\n");
        display_upgrade_message();
        exit_fail(line_info);
    }

    void Checks::check_maybe_upgrade(const LineInfo& line_info, bool expression)
    {
        if (!expression)
        {
            exit_maybe_upgrade(line_info);
        }
    }

    void Checks::check_maybe_upgrade(const LineInfo& line_info, bool expression, StringView error_message)
    {
        if (!expression)
        {
            exit_maybe_upgrade(line_info, error_message);
        }
    }

    void Checks::msg_check_maybe_upgrade(const LineInfo& line_info,
                                         bool expression,
                                         const LocalizedString& error_message)
    {
        if (!expression)
        {
            msg_exit_maybe_upgrade(line_info, error_message);
        }
    }
}
