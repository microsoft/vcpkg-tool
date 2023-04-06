#include <vcpkg/base/checks.h>

#include <vcpkg/commands.acquire-project.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    void AcquireProjectCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Checks::exit_with_code(
            VCPKG_LINE_INFO,
            run_configure_environment_command(paths, "acquire-project", args.get_forwardable_arguments()));
    }
}
