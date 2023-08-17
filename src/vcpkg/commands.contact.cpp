#include <vcpkg/base/chrono.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.contact.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::Contact
{

    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("contact"); },
        0,
        0,
        {{}, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        (void)fs;
        const ParsedArguments parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

        msg::println(msgEmailVcpkgTeam, msg::url = "vcpkg@microsoft.com");

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
