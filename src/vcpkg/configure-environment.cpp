#include <vcpkg/base/basic_checks.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/archives.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg
{
    int run_configure_environment_command(const VcpkgPaths& paths, StringView arg0, View<std::string> args)
    {
        print2(Color::warning, "vcpkg-ce is experimental and may change at any time.\n");

        auto& fs = paths.get_filesystem();
        auto& download_manager = paths.get_download_manager();
        auto node_path = paths.get_tool_exe(Tools::NODE);
        auto node_root = node_path.parent_path();
        auto node_modules = paths.root / "node_modules";
        auto ce_path = node_modules / "vcpkg-ce";
        if (!fs.is_directory(ce_path))
        {
            // We will not ship an official vcpkg-tool release; the means to bundle this with vcpkg-tool proper is still
            // under development.
            print2(Color::warning,
                   "vcpkg-ce is not bootstrapped correctly. Attempting download of latest CE components.\n");
            const auto ce_tarball = paths.downloads / "ce.tgz";
            download_manager.download_file(fs, "https://aka.ms/vcpkg-ce.tgz", ce_tarball, nullopt);
            auto npm_path = Path(node_root) / "node_modules" / "npm" / "bin" / "npm-cli.js";
            Command cmd_provision(node_path);
            cmd_provision.string_arg(npm_path);
            cmd_provision.string_arg("--force");
            cmd_provision.string_arg("install");
            cmd_provision.string_arg("--no-save");
            cmd_provision.string_arg("--no-lockfile");
            cmd_provision.string_arg("--scripts-prepend-node-path=true");
            cmd_provision.string_arg("--silent");
            cmd_provision.string_arg(ce_tarball.native());
            const auto provision_status = cmd_execute(cmd_provision, InWorkingDirectory{paths.root});
            if (provision_status != 0)
            {
                fs.remove_all(node_modules, VCPKG_LINE_INFO);
                Checks::exit_with_message(VCPKG_LINE_INFO, "Failed to provision vcpkg-ce.");
            }
        }

        Command cmd_run(node_path);
        cmd_run.string_arg("--harmony");
        cmd_run.string_arg(ce_path);
        cmd_run.string_arg("--accept-eula");
        cmd_run.string_arg(arg0);
        cmd_run.forwarded_args(args);
        return cmd_execute(cmd_run, InWorkingDirectory{paths.original_cwd});
    }
}
