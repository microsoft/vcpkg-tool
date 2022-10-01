#include <vcpkg/base/chrono.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.contact.h>
#include <vcpkg/help.h>
#include <vcpkg/userconfig.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::Contact
{
    static constexpr StringLiteral OPTION_SURVEY = "survey";

    static constexpr std::array<CommandSwitch, 1> SWITCHES = {{
        {OPTION_SURVEY, "Launch default browser to the current vcpkg survey"},
    }};

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("contact"),
        0,
        0,
        {SWITCHES, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs)
    {
        const ParsedArguments parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

        if (Util::Sets::contains(parsed_args.switches, SWITCHES[0].name))
        {
            auto maybe_now = CTime::get_current_date_time();
            if (const auto p_now = maybe_now.get())
            {
                auto config = UserConfig::try_read_data(fs);
                config.last_completed_survey = p_now->to_string();
                config.try_write_data(fs);
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

    void ContactCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        Contact::perform_and_exit(args, fs);
    }
}
