#include <vcpkg/base/basic_checks.h>

#include <vcpkg/commands.activate-msbuildprops.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    void ActivateMSBuildPropsCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Checks::exit_with_code(
            VCPKG_LINE_INFO,
            run_configure_environment_command(paths, "activate-msbuildprops", args.get_forwardable_arguments()));
    }
}
