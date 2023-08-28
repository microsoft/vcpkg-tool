#include <vcpkg/base/checks.h>

#include <vcpkg/commands.generate-msbuild-props.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    void command_generate_msbuild_props_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        Checks::exit_with_code(
            VCPKG_LINE_INFO,
            run_configure_environment_command(paths, "generate-msbuild-props", args.get_forwardable_arguments()));
    }
}
