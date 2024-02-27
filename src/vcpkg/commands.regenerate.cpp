#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.regenerate.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <string>
#include <vector>

using namespace vcpkg;

namespace
{
    constexpr CommandSwitch command_switches[] = {
        {SwitchForce, msgCmdRegenerateOptForce},
        {SwitchDryRun, msgCmdRegenerateOptDryRun},
        {SwitchNormalize, msgCmdRegenerateOptNormalize},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandRegenerateMetadata{
        "x-regenerate",
        msgRegeneratesArtifactRegistry,
        {"vcpkg x-regenerate"},
        Undocumented,
        AutocompletePriority::Public,
        1,
        1,
        {command_switches},
        nullptr,
    };

    void command_regenerate_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        std::vector<std::string> forwarded_args;
        forwarded_args.emplace_back("regenerate");
        const auto parsed = args.parse_arguments(CommandRegenerateMetadata);
        forwarded_args.push_back(parsed.command_arguments[0]);

        if (Util::Sets::contains(parsed.switches, SwitchForce))
        {
            forwarded_args.emplace_back("--force");
        }

        if (Util::Sets::contains(parsed.switches, SwitchDryRun))
        {
            forwarded_args.emplace_back("--what-if");
        }

        if (Util::Sets::contains(parsed.switches, SwitchNormalize))
        {
            forwarded_args.emplace_back("--normalize");
        }

        Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, forwarded_args));
    }
} // namespace vcpkg
