#include <vcpkg/base/basic_checks.h>
#include <vcpkg/base/stringliteral.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.regenerate.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <array>
#include <string>
#include <vector>

namespace
{
    using namespace vcpkg;

    constexpr StringLiteral DRY_RUN = "dry-run";
    constexpr StringLiteral FORCE = "force";
    constexpr StringLiteral PROJECT = "project";
    constexpr StringLiteral REGISTRY = "registry";

    constexpr std::array<CommandSwitch, 2> command_switches = {{
        {FORCE, "proceeds with the (potentially dangerous) action without confirmation"},
        {DRY_RUN, "does not actually perform the action, shows only what would be done"},
    }};

    constexpr std::array<CommandSetting, 2> command_settings = {{
        {PROJECT, "override the path to the project folder"},
        {REGISTRY, "override the path to the registry"},
    }};

    static const CommandStructure command_structure = {
        Strings::format("Regenerates an artifact registry.\n%s\n", create_example_string("x-regenerate")),
        0,
        0,
        {command_switches, command_settings},
        nullptr,
    };

    static void forward_setting(std::vector<std::string>& forwarded_args,
                                const std::unordered_map<std::string, std::string>& settings,
                                StringLiteral name)
    {
        auto found_setting = settings.find(name);
        if (found_setting != settings.end())
        {
            forwarded_args.push_back(Strings::concat("--", name));
            forwarded_args.push_back(found_setting->second);
        }
    }
}

namespace vcpkg
{
    void RegenerateCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        std::vector<std::string> forwarded_args;
        forwarded_args.push_back("regenerate");
        const auto parsed = args.parse_arguments(command_structure);
        if (Debug::g_debugging)
        {
            forwarded_args.push_back("--debug");
        }

        if (Util::Sets::contains(parsed.switches, FORCE))
        {
            forwarded_args.push_back("--force");
        }

        if (Util::Sets::contains(parsed.switches, DRY_RUN))
        {
            forwarded_args.push_back("--what-if");
        }

        forward_setting(forwarded_args, parsed.settings, PROJECT);
        forward_setting(forwarded_args, parsed.settings, REGISTRY);

        Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, forwarded_args));
    }
}
