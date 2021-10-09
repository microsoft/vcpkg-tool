#include <vcpkg/base/files.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands
{
    struct GenerateDefaultMessageMapCommand : BasicCommand
    {
        void perform_and_exit(const VcpkgCmdArguments&, Filesystem&) const override;
    };
}
