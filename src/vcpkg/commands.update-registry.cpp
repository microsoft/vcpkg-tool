#include <vcpkg/base/checks.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.update-registry.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace
{
    using namespace vcpkg;

    constexpr StringLiteral OPTION_ALL = "all";
    constexpr std::array<CommandSwitch, 1> UpdateRegistrySwitches{{
        {OPTION_ALL, []() { return msg::format(msgCmdUpdateRegistryAll); }},
    }};

    constexpr CommandStructure UpdateRegistryCommandMetadata{
        []() {
            return create_example_string("x-update-registry https://example.com")
                .append_raw("\n")
                .append(create_example_string("x-update-registry microsoft"));
        },
        0,
        SIZE_MAX,
        {UpdateRegistrySwitches, {}, {}},
        nullptr};
} // unnamed namespace

namespace vcpkg::Commands
{
    void command_update_registry_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed = args.parse_arguments(UpdateRegistryCommandMetadata);
        const bool all = Util::Sets::contains(parsed.switches, OPTION_ALL);
        auto&& command_arguments = parsed.command_arguments;
        if (all)
        {
            if (command_arguments.empty())
            {
                command_arguments.emplace_back("update");
                command_arguments.emplace_back("--all");
            }
            else
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgCmdUpdateRegistryAllExcludesTargets);
            }
        }
        else
        {
            if (command_arguments.empty())
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgCmdUpdateRegistryAllOrTargets);
            }
            else
            {
                command_arguments.emplace(command_arguments.begin(), "update");
            }
        }

        Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, command_arguments));
    }
} // vcpkg::Commands
