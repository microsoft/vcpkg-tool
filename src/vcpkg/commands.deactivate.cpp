#include <vcpkg/base/checks.h>

#include <vcpkg/commands.deactivate.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    constexpr CommandMetadata CommandDeactivateMetadata = {[] { return create_example_string("deactivate"); }, 0, 0};

    void command_deactivate_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        (void)args.parse_arguments(CommandDeactivateMetadata);
        Checks::exit_with_code(VCPKG_LINE_INFO,
                               run_configure_environment_command(paths, "deactivate", View<std::string>{}));
    }
}
