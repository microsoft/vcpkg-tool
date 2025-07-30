#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/optional.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.bootstrap-standalone.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <string>

namespace
{
    using namespace vcpkg;

    Optional<Path> download_vcpkg_standalone_bundle(DiagnosticContext& context,
                                                    const AssetCachingSettings& asset_cache_settings,
                                                    const Filesystem& fs,
                                                    const Path& download_root)
    {
#if defined(VCPKG_STANDALONE_BUNDLE_SHA)
        static constexpr StringLiteral tarball_name = "vcpkg-standalone-bundle-" VCPKG_BASE_VERSION_AS_STRING ".tar.gz";
        const auto bundle_tarball = download_root / tarball_name;
        context.statusln(msg::format(msgDownloadingVcpkgStandaloneBundle, msg::version = VCPKG_BASE_VERSION_AS_STRING));
        const auto bundle_uri =
            "https://github.com/microsoft/vcpkg-tool/releases/download/" VCPKG_BASE_VERSION_AS_STRING
            "/vcpkg-standalone-bundle.tar.gz";
        if (!download_file_asset_cached(context,
                                        null_sink,
                                        asset_cache_settings,
                                        fs,
                                        bundle_uri,
                                        {},
                                        bundle_tarball,
                                        tarball_name,
                                        MACRO_TO_STRING(VCPKG_STANDALONE_BUNDLE_SHA)))
        {
            return nullopt;
        }
#else  // ^^^ VCPKG_STANDALONE_BUNDLE_SHA / !VCPKG_STANDALONE_BUNDLE_SHA vvv
        static constexpr StringLiteral latest_tarball_name = "vcpkg-standalone-bundle-latest.tar.gz";
        const auto bundle_tarball = download_root / latest_tarball_name;
        context.report(DiagnosticLine{DiagKind::Warning, msg::format(msgDownloadingVcpkgStandaloneBundleLatest)});
        std::error_code ec;
        fs.remove(bundle_tarball, ec);
        if (ec)
        {
            context.report_error(format_filesystem_call_error(ec, "remove", {bundle_tarball}));
            return nullopt;
        }

        const auto bundle_uri =
            "https://github.com/microsoft/vcpkg-tool/releases/latest/download/vcpkg-standalone-bundle.tar.gz";
        if (!download_file_asset_cached(context,
                                        null_sink,
                                        asset_cache_settings,
                                        fs,
                                        bundle_uri,
                                        {},
                                        bundle_tarball,
                                        latest_tarball_name,
                                        nullopt))
        {
            return nullopt;
        }
#endif // ^^^ !VCPKG_STANDALONE_BUNDLE_SHA
        return bundle_tarball;
    }
}

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

        AssetCachingSettings asset_cache_settings;
        const auto maybe_vcpkg_root_env = args.vcpkg_root_dir_env.get();
        if (!maybe_vcpkg_root_env)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgVcpkgRootRequired);
        }

        const auto vcpkg_root = fs.almost_canonical(*maybe_vcpkg_root_env, VCPKG_LINE_INFO);
        fs.create_directories(vcpkg_root, VCPKG_LINE_INFO);
        auto maybe_tarball =
            download_vcpkg_standalone_bundle(console_diagnostic_context, asset_cache_settings, fs, vcpkg_root);
        auto tarball = maybe_tarball.get();
        if (!tarball)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        extract_tar(find_system_tar(fs).value_or_exit(VCPKG_LINE_INFO), *tarball, vcpkg_root);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
