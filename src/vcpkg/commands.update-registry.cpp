#include <vcpkg/base/checks.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.update-registry.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral OPTION_ALL = "all";
    constexpr CommandSwitch UpdateRegistrySwitches[] = {
        {OPTION_ALL, msgCmdUpdateRegistryAll},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandUpdateRegistryMetadata{
        "x-update-registry",
        msgCmdUpdateRegistrySynopsis,
        {
            "vcpkg x-update-registry <uri>",
            "vcpkg x-update-registry https://example.com",
            msgCmdUpdateRegistryExample3,
            "vcpkg x-update-registry microsoft",
        },
        Undocumented,
        AutocompletePriority::Public,
        0,
        SIZE_MAX,
        {UpdateRegistrySwitches},
        nullptr,
    };

    void command_update_registry_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed = args.parse_arguments(CommandUpdateRegistryMetadata);
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
} // namespace vcpkg
