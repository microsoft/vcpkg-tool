#include <vcpkg/base/basic_checks.h>

#include <vcpkg/commands.activate.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::Activate
{
    void ActivateCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Checks::exit_with_code(VCPKG_LINE_INFO,
                               run_configure_environment_command(paths, "activate", args.get_forwardable_arguments()));
    }
}
