#include <vcpkg/base/system_headers.h>

#include <vcpkg/base/chrono.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/pragmas.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/commands.contact.h>
#include <vcpkg/commands.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/userconfig.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

#include <locale.h>

#include <cassert>
#include <memory>
#include <random>

#if defined(_WIN32)
#pragma comment(lib, "ole32")
#pragma comment(lib, "shell32")
#endif

using namespace vcpkg;

namespace
{
    DECLARE_AND_REGISTER_MESSAGE(VcpkgInvalidCommand, (msg::value), "", "invalid command: {value}");
    DECLARE_AND_REGISTER_MESSAGE(VcpkgDebugTimeTaken,
                                 (msg::pretty_value, msg::value),
                                 "{LOCKED}",
                                 "[DEBUG] Exiting after %s (%d us)\n");
    DECLARE_AND_REGISTER_MESSAGE(VcpkgSendMetricsButDisabled,
                                 (),
                                 "",
                                 "Warning: passed --sendmetrics, but metrics are disabled.");
    DECLARE_AND_REGISTER_MESSAGE(VcpkgHasCrashed,
                                 (msg::email, msg::version, msg::error),
                                 "",
                                 R"(vcpkg.exe has crashed.
Please send an email to:
    {email}
containing a brief summary of what you were trying to do and the following data blob:

Version={vcpkg_version}
EXCEPTION='{error}'
CMD=)");
    DECLARE_AND_REGISTER_MESSAGE(VcpkgHasCrashedArgument, (msg::value), "{LOCKED}", "{value}|");
}

// 24 hours/day * 30 days/month * 6 months
static constexpr int SURVEY_INTERVAL_IN_HOURS = 24 * 30 * 6;

// Initial survey appears after 10 days. Therefore, subtract 24 hours/day * 10 days
static constexpr int SURVEY_INITIAL_OFFSET_IN_HOURS = SURVEY_INTERVAL_IN_HOURS - 24 * 10;

static void invalid_command(const std::string& cmd)
{
    msg::println(Color::error, msgVcpkgInvalidCommand, msg::value = cmd);
    print_usage();
    Checks::exit_fail(VCPKG_LINE_INFO);
}

static void inner(vcpkg::Filesystem& fs, const VcpkgCmdArguments& args)
{
    LockGuardPtr<Metrics>(g_metrics)->track_property("command", args.command);
    if (args.command.empty())
    {
        print_usage();
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    static const auto find_command = [&](auto&& commands) {
        auto it = Util::find_if(commands, [&](auto&& commandc) {
            return Strings::case_insensitive_ascii_equals(commandc.name, args.command);
        });
        using std::end;
        if (it != end(commands))
        {
            return &*it;
        }
        else
        {
            return static_cast<decltype(&*it)>(nullptr);
        }
    };

    if (const auto command_function = find_command(Commands::get_available_basic_commands()))
    {
        return command_function->function->perform_and_exit(args, fs);
    }

    const VcpkgPaths paths(fs, args);
    paths.track_feature_flag_metrics();

    fs.current_path(paths.root, VCPKG_LINE_INFO);

    if (const auto command_function = find_command(Commands::get_available_paths_commands()))
    {
        return command_function->function->perform_and_exit(args, paths);
    }

    Triplet default_triplet = vcpkg::default_triplet(args);
    Input::check_triplet(default_triplet, paths);
    Triplet host_triplet = vcpkg::default_host_triplet(args);
    Input::check_triplet(host_triplet, paths);

    if (const auto command_function = find_command(Commands::get_available_triplet_commands()))
    {
        return command_function->function->perform_and_exit(args, paths, default_triplet, host_triplet);
    }

    return invalid_command(args.command);
}

static void load_config(vcpkg::Filesystem& fs)
{
    auto config = UserConfig::try_read_data(fs);

    bool write_config = false;

    // config file not found, could not be read, or invalid
    if (config.user_id.empty() || config.user_time.empty())
    {
        ::vcpkg::Metrics::init_user_information(config.user_id, config.user_time);
        write_config = true;
    }

#if defined(_WIN32)
    if (config.user_mac.empty())
    {
        config.user_mac = get_MAC_user();
        write_config = true;
    }
#endif

    {
        LockGuardPtr<Metrics> locked_metrics(g_metrics);
        locked_metrics->set_user_information(config.user_id, config.user_time);
#if defined(_WIN32)
        locked_metrics->track_property("user_mac", config.user_mac);
#endif
    }

    if (config.last_completed_survey.empty())
    {
        const auto now = CTime::parse(config.user_time).value_or_exit(VCPKG_LINE_INFO);
        const CTime offset = now.add_hours(-SURVEY_INITIAL_OFFSET_IN_HOURS);
        config.last_completed_survey = offset.to_string();
    }

    LockGuardPtr<std::string>(GlobalState::g_surveydate)->assign(config.last_completed_survey);

    if (write_config)
    {
        config.try_write_data(fs);
    }
}

#if defined(_WIN32)
// note: this prevents a false positive for -Wmissing-prototypes on clang-cl
int wmain(int, const wchar_t* const* const);

#if !defined(_MSC_VER)
#include <shellapi.h>
int main(int argc, const char* const* const /*argv*/)
{
    wchar_t** wargv;
    wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    return wmain(argc, wargv);
}
#endif

int wmain(const int argc, const wchar_t* const* const argv)
#else
int main(const int argc, const char* const* const argv)
#endif
{
    if (argc == 0) std::abort();

    auto& fs = get_real_filesystem();
    {
        auto locale = get_environment_variable("VCPKG_LOCALE");
        auto locale_base = get_environment_variable("VCPKG_LOCALE_BASE");

        if (locale.has_value() && locale_base.has_value())
        {
            msg::threadunsafe_initialize_context(fs, *locale.get(), *locale_base.get());
        }
        else if (locale.has_value() || locale_base.has_value())
        {
            msg::write_unlocalized_text_to_stdout(
                Color::error, "If either VCPKG_LOCALE or VCPKG_LOCALE_BASE is initialized, then both must be.\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        else
        {
            msg::threadunsafe_initialize_context();
        }
    }

    *(LockGuardPtr<ElapsedTimer>(GlobalState::timer)) = ElapsedTimer::create_started();

#if defined(_WIN32)
    GlobalState::g_init_console_cp = GetConsoleCP();
    GlobalState::g_init_console_output_cp = GetConsoleOutputCP();
    GlobalState::g_init_console_initialized = true;

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    initialize_global_job_object();
#else
    static const char* const utf8_locales[] = {
        "C.UTF-8",
        "POSIX.UTF-8",
        "en_US.UTF-8",
    };

    for (const char* utf8_locale : utf8_locales)
    {
        if (::setlocale(LC_ALL, utf8_locale))
        {
            ::setenv("LC_ALL", utf8_locale, true);
            break;
        }
    }
#endif
    set_environment_variable("VCPKG_COMMAND", get_exe_path_of_current_process().generic_u8string());

    Checks::register_global_shutdown_handler([]() {
        const auto elapsed_us_inner = LockGuardPtr<ElapsedTimer>(GlobalState::timer)->microseconds();

        bool debugging = Debug::g_debugging;

        LockGuardPtr<Metrics> metrics(g_metrics);
        metrics->track_metric("elapsed_us", elapsed_us_inner);
        Debug::g_debugging = false;
        metrics->flush(get_real_filesystem());

#if defined(_WIN32)
        if (GlobalState::g_init_console_initialized)
        {
            SetConsoleCP(GlobalState::g_init_console_cp);
            SetConsoleOutputCP(GlobalState::g_init_console_output_cp);
        }
#endif

        if (debugging)
            msg::println(msgVcpkgDebugTimeTaken,
                         msg::pretty_value = LockGuardPtr<ElapsedTimer>(GlobalState::timer)->to_string(),
                         msg::value = static_cast<int64_t>(elapsed_us_inner));
    });

    LockGuardPtr<Metrics>(g_metrics)->track_property("version", Commands::Version::version());

    register_console_ctrl_handler();

    load_config(fs);

#if (defined(__aarch64__) || defined(__arm__) || defined(__s390x__) ||                                                 \
     ((defined(__ppc64__) || defined(__PPC64__) || defined(__ppc64le__) || defined(__PPC64LE__)) &&                    \
      defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)) ||                                       \
     defined(_M_ARM) || defined(_M_ARM64)) &&                                                                          \
    !defined(_WIN32) && !defined(__APPLE__)
    if (!get_environment_variable("VCPKG_FORCE_SYSTEM_BINARIES").has_value())
    {
        Checks::exit_with_message(
            VCPKG_LINE_INFO,
            "Environment variable VCPKG_FORCE_SYSTEM_BINARIES must be set on arm, s390x, and ppc64le platforms.");
    }
#endif

    VcpkgCmdArguments args = VcpkgCmdArguments::create_from_command_line(fs, argc, argv);
    if (const auto p = args.debug.get()) Debug::g_debugging = *p;
    args.imbue_from_environment();
    VcpkgCmdArguments::imbue_or_apply_process_recursion(args);
    args.check_feature_flag_consistency();

    {
        LockGuardPtr<Metrics> metrics(g_metrics);
        if (const auto p = args.disable_metrics.get())
        {
            metrics->set_disabled(*p);
        }

        auto disable_metrics_tag_file_path = get_exe_path_of_current_process();
        disable_metrics_tag_file_path.replace_filename("vcpkg.disable-metrics");

        std::error_code ec;
        if (fs.exists(disable_metrics_tag_file_path, ec) || ec)
        {
            metrics->set_disabled(true);
        }

        if (const auto p = args.print_metrics.get())
        {
            metrics->set_print_metrics(*p);
        }

        if (const auto p = args.send_metrics.get())
        {
            metrics->set_send_metrics(*p);
        }

        if (args.send_metrics.value_or(false) && !metrics->metrics_enabled())
        {
            msg::println(Color::warning, msgVcpkgSendMetricsButDisabled);
        }
    } // unlock g_metrics

    args.debug_print_feature_flags();
    args.track_feature_flag_metrics();

    if (Debug::g_debugging)
    {
        inner(fs, args);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    std::string exc_msg;
    try
    {
        inner(fs, args);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
    catch (std::exception& e)
    {
        exc_msg = e.what();
    }
    catch (...)
    {
        exc_msg = "unknown error(...)";
    }

    LockGuardPtr<Metrics>(g_metrics)->track_property("error", exc_msg);

    fflush(stdout);
    msg::println(msgVcpkgHasCrashed,
                 msg::email = Commands::Contact::email(),
                 msg::version = Commands::Version::version(),
                 msg::error = exc_msg);
    fflush(stdout);
    for (int x = 0; x < argc; ++x)
    {
#if defined(_WIN32)
        msg::println(msgVcpkgHasCrashedArgument, msg::value = Strings::to_utf8(argv[x]));
#else
        msg::println(msgVcpkgHasCrashedArgument, msg::value = argv[x]);
#endif
    }
    fflush(stdout);

    // It is expected that one of the sub-commands will exit cleanly before we get here.
    Checks::exit_fail(VCPKG_LINE_INFO);
}
