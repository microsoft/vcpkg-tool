#include <vcpkg/base/basic_checks.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg
{
    int run_configure_environment_command(const VcpkgPaths& paths, StringView arg0, View<std::string> args)
    {
        print2(Color::warning, "vcpkg-ce ('configure environment') is experimental and may change at any time.\n");

        auto& fs = paths.get_filesystem();
        auto& download_manager = paths.get_download_manager();
        auto node_path = paths.get_tool_exe(Tools::NODE);
        auto node_root = node_path.parent_path();
        auto node_modules = paths.root / "node_modules";
        auto ce_path = node_modules / "vcpkg-ce";
        if (!fs.is_directory(ce_path))
        {
            auto env = get_modified_clean_environment({}, node_root);
#if defined(VCPKG_CE_SHA)
            print2("Downloading vcpkg-ce bundle " VCPKG_BASE_VERSION_AS_STRING "\n");
            const auto ce_uri =
                "https://github.com/microsoft/vcpkg-tool/releases/download/" VCPKG_BASE_VERSION_AS_STRING
                "/vcpkg-ce.tgz";
            const auto ce_tarball = paths.downloads / "vcpkg-ce-" VCPKG_BASE_VERSION_AS_STRING ".tgz";
            download_manager.download_file(fs, ce_uri, ce_tarball, std::string(MACRO_TO_STRING(VCPKG_CE_SHA)));
#else  // ^^^ VCPKG_CE_BUNDLE_SHA / !VCPKG_CE_BUNDLE_SHA vvv
            print2(Color::warning, "Downloading latest vcpkg-ce bundle\n");
            const auto ce_uri = "https://github.com/microsoft/vcpkg-tool/releases/latest/download/vcpkg-ce.tgz";
            const auto ce_tarball = paths.downloads / "vcpkg-ce-latest.tgz";
            download_manager.download_file(fs, ce_uri, ce_tarball, nullopt);
#endif // ^^^ !VCPKG_CE_BUNDLE_SHA
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
            const auto provision_status = cmd_execute(cmd_provision, InWorkingDirectory{paths.root}, env);
            if (provision_status != 0)
            {
                fs.remove(ce_tarball, VCPKG_LINE_INFO);
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
        // tell vcpkg-ce that it's being called from vcpkg
        cmd_run.string_arg("--from-vcpkg");
        return cmd_execute(cmd_run, InWorkingDirectory{paths.original_cwd});
    }
}
