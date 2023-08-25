#include <vcpkg/base/checks.h>

#include <vcpkg/commands.z-ce.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    constexpr CommandMetadata CommandZCEMetadata{
        "z-ce",
        {/*intentionally undocumented*/},
        {},
        AutocompletePriority::Never,
        0,
        SIZE_MAX,
        {},
        nullptr,
    };

    void command_z_ce_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        Checks::exit_with_code(VCPKG_LINE_INFO,
                               run_configure_environment_command(paths, args.get_forwardable_arguments()));
    }
}
