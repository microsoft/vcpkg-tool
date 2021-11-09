#include <vcpkg/base/system.process.h>
#include <vcpkg/base/basic_checks.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/tools.h>
#include <vcpkg/commands.activate.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands::Activate
{
    void ActivateCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        print2(Color::warning, "vcpkg-ce is experimental and may change at any time.\n");

        auto node_path = paths.get_tool_exe(Tools::NODE);
        auto ce_path = paths.root / "node_modules/vcpkg-ce";

        if (!paths.get_filesystem().is_directory(ce_path))
        {
            auto npm_path = Path(node_path.parent_path()) / "node_modules" / "npm" / "bin" / "npm-cli.js";
            Command cmd_provision(node_path);
            cmd_provision.string_arg(npm_path);
            cmd_provision.string_arg("--force");
            cmd_provision.string_arg("install");
            cmd_provision.string_arg("--no-save");
            cmd_provision.string_arg("--no-lockfile");
            cmd_provision.string_arg("--scripts-prepend-node-path=true");
            cmd_provision.string_arg("https://aka.ms/vcpkg-ce.tgz");
            const auto provision_status = cmd_execute(cmd_provision);
            if (provision_status != 0)
            {
                Checks::exit_with_message(VCPKG_LINE_INFO, "Failed to provision vcpkg-ce.");
            }
        }

        Command cmd_run(node_path);
        cmd_run.string_arg("--harmony");
        cmd_run.string_arg(ce_path.native());
        cmd_run.string_arg("--accept-eula");
        cmd_run.string_arg("activate");
        args.add_forwarded_arguments(cmd_run);
        Checks::exit_with_code(VCPKG_LINE_INFO, cmd_execute(cmd_run));
    }
}
