#include <vcpkg/base/basic_checks.h>

#include <vcpkg/commands.use.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    void UseCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Checks::exit_with_code(VCPKG_LINE_INFO,
                               run_configure_environment_command(paths, "use", args.get_forwardable_arguments()));
    }
}
