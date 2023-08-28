#include <vcpkg/base/chrono.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.contact.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    constexpr CommandMetadata CommandContactMetadata = {
        [] { return create_example_string("contact"); },
        0,
        0,
        {{}, {}},
        nullptr,
    };

    void command_contact_and_exit(const VcpkgCmdArguments& args, const Filesystem&)
    {
        (void)args.parse_arguments(CommandContactMetadata);
        msg::println(msgEmailVcpkgTeam, msg::url = "vcpkg@microsoft.com");
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
