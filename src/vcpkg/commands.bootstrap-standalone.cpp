#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.bootstrap-standalone.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <string>

namespace vcpkg
{
    constexpr CommandMetadata CommandBootstrapStandaloneMetadata{
        "bootstrap-standalone",
        msgCmdBootstrapStandaloneSynopsis,
        {"vcpkg bootstrap-standalone"},
        Undocumented,
        AutocompletePriority::Never,
        0,
        0,
        {},
        nullptr,
    };

    void command_bootstrap_standalone_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        (void)args.parse_arguments(CommandBootstrapStandaloneMetadata);

        DownloadManager download_manager{{}};
        const auto maybe_vcpkg_root_env = args.vcpkg_root_dir_env.get();
        if (!maybe_vcpkg_root_env)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgVcpkgRootRequired);
        }

        const auto vcpkg_root = fs.almost_canonical(*maybe_vcpkg_root_env, VCPKG_LINE_INFO);
        fs.create_directories(vcpkg_root, VCPKG_LINE_INFO);
        auto tarball =
            download_vcpkg_standalone_bundle(download_manager, fs, vcpkg_root).value_or_exit(VCPKG_LINE_INFO);
        fs.remove_all(vcpkg_root / "vcpkg-artifacts", VCPKG_LINE_INFO);
        extract_tar(find_system_tar(fs).value_or_exit(VCPKG_LINE_INFO), tarball, vcpkg_root);
        fs.remove(tarball, VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
