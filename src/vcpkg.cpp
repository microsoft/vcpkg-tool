#include <vcpkg/base/system_headers.h>

#include <vcpkg/base/chrono.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/pragmas.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
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

#include <cassert>
#include <memory>
#include <random>

#if defined(_WIN32)
#pragma comment(lib, "ole32")
#pragma comment(lib, "shell32")
#endif

using namespace vcpkg;

// 24 hours/day * 30 days/month * 6 months
static constexpr int SURVEY_INTERVAL_IN_HOURS = 24 * 30 * 6;

// Initial survey appears after 10 days. Therefore, subtract 24 hours/day * 10 days
static constexpr int SURVEY_INITIAL_OFFSET_IN_HOURS = SURVEY_INTERVAL_IN_HOURS - 24 * 10;

static void invalid_command(const std::string& cmd)
{
    print2(Color::error, "invalid command: ", cmd, '\n');
    print_usage();
    Checks::exit_fail(VCPKG_LINE_INFO);
}

static void inner(vcpkg::Filesystem& fs, const VcpkgCmdArguments& args)
{
    Metrics::g_metrics.lock()->track_property("command", args.command);
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
        ::vcpkg::Metrics::Metrics::init_user_information(config.user_id, config.user_time);
        write_config = true;
    }

#if defined(_WIN32)
    if (config.user_mac.empty())
    {
        config.user_mac = Metrics::get_MAC_user();
        write_config = true;
    }
#endif

    {
        auto locked_metrics = Metrics::g_metrics.lock();
        locked_metrics->set_user_information(config.user_id, config.user_time);
#if defined(_WIN32)
        locked_metrics->track_property("user_mac", config.user_mac);
#endif
    }

    if (config.last_completed_survey.empty())
    {
        const auto now = Chrono::CTime::parse(config.user_time).value_or_exit(VCPKG_LINE_INFO);
        const Chrono::CTime offset = now.add_hours(-SURVEY_INITIAL_OFFSET_IN_HOURS);
        config.last_completed_survey = offset.to_string();
    }

    GlobalState::g_surveydate.lock()->assign(config.last_completed_survey);

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
    *GlobalState::timer.lock() = Chrono::ElapsedTimer::create_started();

#if defined(_WIN32)
    GlobalState::g_init_console_cp = GetConsoleCP();
    GlobalState::g_init_console_output_cp = GetConsoleOutputCP();
    GlobalState::g_init_console_initialized = true;

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    initialize_global_job_object();
#endif
    set_environment_variable("VCPKG_COMMAND", vcpkg::generic_u8string(get_exe_path_of_current_process()));

    Checks::register_global_shutdown_handler([]() {
        const auto elapsed_us_inner = GlobalState::timer.lock()->microseconds();

        bool debugging = Debug::g_debugging;

        auto metrics = Metrics::g_metrics.lock();
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

        auto elapsed_us = GlobalState::timer.lock()->microseconds();
        if (debugging)
            vcpkg::printf("[DEBUG] Exiting after %d us (%d us)\n",
                          static_cast<int>(elapsed_us),
                          static_cast<int>(elapsed_us_inner));
    });

    {
        auto locked_metrics = Metrics::g_metrics.lock();
        locked_metrics->track_property("version", Commands::Version::version());
    }

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
        auto metrics = Metrics::g_metrics.lock();
        if (const auto p = args.disable_metrics.get())
        {
            metrics->set_disabled(*p);
        }

        auto disable_metrics_tag_file_path =
            get_exe_path_of_current_process().replace_filename(vcpkg::u8path("vcpkg.disable-metrics"));
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
            print2(Color::warning, "Warning: passed --sendmetrics, but metrics are disabled.\n");
        }
    } // unlock Metrics::g_metrics

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
    Metrics::g_metrics.lock()->track_property("error", exc_msg);

    fflush(stdout);
    vcpkg::printf("vcpkg.exe has crashed.\n"
                  "Please send an email to:\n"
                  "    %s\n"
                  "containing a brief summary of what you were trying to do and the following data blob:\n"
                  "\n"
                  "Version=%s\n"
                  "EXCEPTION='%s'\n"
                  "CMD=\n",
                  Commands::Contact::email(),
                  Commands::Version::version(),
                  exc_msg);
    fflush(stdout);
    for (int x = 0; x < argc; ++x)
    {
#if defined(_WIN32)
        print2(Strings::to_utf8(argv[x]), "|\n");
#else
        print2(argv[x], "|\n");
#endif
    }
    fflush(stdout);

    // It is expected that one of the sub-commands will exit cleanly before we get here.
    Checks::exit_fail(VCPKG_LINE_INFO);
}
