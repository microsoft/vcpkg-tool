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
        const auto& vcpkg_root_arg = args.vcpkg_root_dir;
        if (!vcpkg_root_arg)
        {
            Checks::exit_with_message(VCPKG_LINE_INFO, "Setting VCPKG_ROOT is required for standalone bootstrap.\n");
        }

        const auto& vcpkg_root = fs.almost_canonical(*vcpkg_root_arg, VCPKG_LINE_INFO);
        fs.create_directories(vcpkg_root, VCPKG_LINE_INFO);
        const auto bundle_tarball = vcpkg_root / "vcpkg-standalone-bundle.tar.gz";
#if defined(VCPKG_STANDALONE_BUNDLE_SHA)
        print2("Downloading vcpkg standlone bundle " VCPKG_BASE_VERSION_AS_STRING "\n");
        const auto bundle_uri =
            "https://github.com/microsoft/vcpkg-tool/releases/download/" VCPKG_BASE_VERSION_AS_STRING
            "/vcpkg-standalone-bundle.tar.gz";
        download_manager.download_file(
            fs, bundle_uri, bundle_tarball, std::string(MACRO_TO_STRING(VCPKG_STANDALONE_BUNDLE_SHA)));
#else  // ^^^ VCPKG_STANDALONE_BUNDLE_SHA / !VCPKG_STANDALONE_BUNDLE_SHA vvv
        print2(Color::warning, "Downloading latest standalone bundle\n");
        const auto bundle_uri =
            "https://github.com/microsoft/vcpkg-tool/releases/latest/download/vcpkg-standalone-bundle.tar.gz";
        download_manager.download_file(fs, bundle_uri, bundle_tarball, nullopt);
#endif // ^^^ !VCPKG_STANDALONE_BUNDLE_SHA

        auto tool_cache = get_tool_cache(RequireExactVersions::NO);
        const auto tar = tool_cache->get_tool_path_from_system(fs, Tools::TAR);
        extract_tar(tar, bundle_tarball, vcpkg_root);
        fs.remove(bundle_tarball, VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
