#include <vcpkg/base/chrono.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.contact.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::Contact
{
    static constexpr StringLiteral OPTION_SURVEY = "survey";

    static constexpr std::array<CommandSwitch, 1> SWITCHES = {{
        {OPTION_SURVEY, []() { return msg::format(msgCmdContactOptSurvey); }},
    }};

    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("contact"); },
        0,
        0,
        {SWITCHES, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        const ParsedArguments parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

        if (Util::Sets::contains(parsed_args.switches, SWITCHES[0].name))
        {
            auto maybe_now = CTime::now();
            if (const auto p_now = maybe_now.get())
            {
                auto config = try_read_metrics_user(fs);
                config.last_completed_survey = p_now->to_string();
                config.try_write(fs);
            }

#if defined(_WIN32)
            cmd_execute(Command("start").string_arg("https://aka.ms/NPS_vcpkg"));
            msg::println(msgDefaultBrowserLaunched, msg::url = "https://aka.ms/NPS_vcpkg");
            msg::println(msgFeedbackAppreciated);
#else
            msg::println(msgNavigateToNPS, msg::url = "https://aka.ms/NPS_vcpkg");
            msg::println(msgFeedbackAppreciated);
#endif
        }
        else
        {
            msg::println(msgEmailVcpkgTeam, msg::url = "vcpkg@microsoft.com");
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
