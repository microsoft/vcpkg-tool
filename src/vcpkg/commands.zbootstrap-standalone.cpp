#include <vcpkg/base/downloads.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/commands.zbootstrap-standalone.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <string>

namespace vcpkg::Commands
{
    static const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("z-bootstrap-standalone"),
        0,
        0,
        {{}, {}},
        nullptr,
    };

    void ZBootstrapStandaloneCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        DownloadManager download_manager{{}};
        const auto maybe_vcpkg_root_arg = args.vcpkg_root_dir.get();
        if (!maybe_vcpkg_root_arg)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgVcpkgRootRequired);
        }

        const auto& vcpkg_root = fs.almost_canonical(*maybe_vcpkg_root_arg, VCPKG_LINE_INFO);
        fs.create_directories(vcpkg_root, VCPKG_LINE_INFO);
        const auto bundle_tarball = vcpkg_root / "vcpkg-standalone-bundle.tar.gz";
#if defined(VCPKG_STANDALONE_BUNDLE_SHA)
        msg::println(msgDownloadingVcpkgStandaloneBundle, msg::version = VCPKG_BASE_VERSION_AS_STRING);
        const auto bundle_uri =
            "https://github.com/microsoft/vcpkg-tool/releases/download/" VCPKG_BASE_VERSION_AS_STRING
            "/vcpkg-standalone-bundle.tar.gz";
        download_manager.download_file(
            fs, bundle_uri, bundle_tarball, std::string(MACRO_TO_STRING(VCPKG_STANDALONE_BUNDLE_SHA)));
#else  // ^^^ VCPKG_STANDALONE_BUNDLE_SHA / !VCPKG_STANDALONE_BUNDLE_SHA vvv
        msg::println(Color::warning, msgDownloadingVcpkgStandaloneBundleLatest);
        const auto bundle_uri =
            "https://github.com/microsoft/vcpkg-tool/releases/latest/download/vcpkg-standalone-bundle.tar.gz";
        download_manager.download_file(fs, bundle_uri, bundle_tarball, nullopt);
#endif // ^^^ !VCPKG_STANDALONE_BUNDLE_SHA

        extract_tar(find_system_tar(fs).value_or_exit(VCPKG_LINE_INFO), bundle_tarball, vcpkg_root);
        fs.remove(bundle_tarball, VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
