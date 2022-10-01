#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.version.h>
#include <vcpkg/help.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;
    constexpr StringLiteral version_init = VCPKG_BASE_VERSION_AS_STRING "-" VCPKG_VERSION_AS_STRING
#ifndef NDEBUG
                                                                        "-debug"
#endif
        ;
}

namespace vcpkg::Commands::Version
{
    const StringLiteral version = version_init;
    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("version"),
        0,
        0,
        {},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, Filesystem&)
    {
        (void)args.parse_arguments(COMMAND_STRUCTURE);
        msg::println(msgVersionCommandHeader, msg::version = version);
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void VersionCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        Version::perform_and_exit(args, fs);
    }
}
