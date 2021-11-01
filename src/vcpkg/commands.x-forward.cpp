#include <vcpkg/base/system.process.h>

#include <vcpkg/commands.x-forward.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::X_Forward
{
    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths&)
    {
        Command cmd("vcpkg-utility-impl");
        cmd.string_arg("--");
        args.add_forwarded_arguments(cmd);
        int status = cmd_execute(cmd);
        Checks::exit_with_code(VCPKG_LINE_INFO, status);
    }

    void XForwardCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        X_Forward::perform_and_exit(args, paths);
    }
}
