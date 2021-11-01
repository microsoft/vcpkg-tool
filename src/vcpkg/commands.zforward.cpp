#include <vcpkg/base/system.process.h>

#include <vcpkg/commands.zforward.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::Z_Forward
{
    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths&)
    {
        Command cmd("insert-executable-name-here");
        cmd.string_arg("--");
        args.add_forwarded_arguments(cmd);
        int status = cmd_execute(cmd);
        Checks::exit_with_code(VCPKG_LINE_INFO, status);
    }

    void ForwardCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Z_Forward::perform_and_exit(args, paths);
    }
}
