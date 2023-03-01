#include <vcpkg/base/basic-checks.h>

#include <vcpkg/commands.deactivate.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace
{
    using namespace vcpkg;

    const CommandStructure COMMAND_STRUCTURE = {[] { return create_example_string("deactivate"); }, 0, 0};
}

namespace vcpkg::Commands
{
    void DeactivateCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        (void)args.parse_arguments(COMMAND_STRUCTURE);
        Checks::exit_with_code(VCPKG_LINE_INFO,
                               run_configure_environment_command(paths, "deactivate", View<std::string>{}));
    }
}
