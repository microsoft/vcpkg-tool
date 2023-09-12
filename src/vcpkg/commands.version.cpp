#include <vcpkg/commands.help.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral version_init = VCPKG_BASE_VERSION_AS_STRING "-" VCPKG_VERSION_AS_STRING
#ifndef NDEBUG
                                                                        "-debug"
#endif
        ;
}

namespace vcpkg
{
    constexpr StringLiteral vcpkg_executable_version = version_init;
    constexpr CommandMetadata CommandVersionMetadata{
        "version",
        msgHelpVersionCommand,
        {"vcpkg version"},
        "https://learn.microsoft.com/vcpkg/commands/version",
        AutocompletePriority::Public,
        0,
        0,
        {},
        nullptr,
    };

    void command_version_and_exit(const VcpkgCmdArguments& args, const Filesystem&)
    {
        (void)args.parse_arguments(CommandVersionMetadata);
        msg::println(msgVersionCommandHeader, msg::version = vcpkg_executable_version);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
