#include <vcpkg/commands.update-baseline.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    static const CommandStructure COMMAND_STRUCTURE{};

    void UpdateBaselineCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const {
        (void)args;
        (void)paths;
    }
}
