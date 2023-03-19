#include <vcpkg/base/basic-checks.h>

#include <vcpkg/commands.acquire.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    void AcquireCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Checks::exit_with_code(VCPKG_LINE_INFO,
                               run_configure_environment_command(paths, "acquire", args.get_forwardable_arguments()));
    }
}
