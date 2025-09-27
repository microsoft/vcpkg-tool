#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/commands.download.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace
{
    constexpr CommandSwitch FETCH_SWITCHES[] = {
        {SwitchStore, msgCmdXDownloadOptStore},
        {SwitchSkipSha512, msgCmdXDownloadOptSkipSha},
        {SwitchZMachineReadableProgress, {}},
    };
    constexpr CommandSetting FETCH_SETTINGS[] = {
        {SwitchSha512, msgCmdXDownloadOptSha},
    };
    constexpr CommandMultiSetting FETCH_MULTISETTINGS[] = {
        {SwitchUrl, msgCmdXDownloadOptUrl},
        {SwitchHeader, msgCmdXDownloadOptHeader},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandDownloadMetadata{
        "x-download",
        msgCmdDownloadSynopsis,
        {msgCmdDownloadExample1, msgCmdDownloadExample2, msgCmdDownloadExample3},
        Undocumented,
        AutocompletePriority::Internal,
        1,
        2,
        {FETCH_SWITCHES, FETCH_SETTINGS, FETCH_MULTISETTINGS},
        nullptr,
    };

    static bool is_hex(StringView sha) { return std::all_of(sha.begin(), sha.end(), ParserBase::is_hex_digit); }
    static bool is_sha512(StringView sha) { return sha.size() == 128 && is_hex(sha); }

    static Optional<std::string> get_sha512_check(const ParsedArguments& parsed)
    {
        Optional<std::string> sha;
        auto sha_it = parsed.settings.find(SwitchSha512);
        if (parsed.command_arguments.size() > 1)
        {
            if (sha_it != parsed.settings.end())
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgShaPassedAsArgAndOption);
            }
            sha = parsed.command_arguments[1];
        }
        else if (sha_it != parsed.settings.end())
        {
            sha = sha_it->second;
        }

        if (Util::Sets::contains(parsed.switches, SwitchSkipSha512))
        {
            if (sha.has_value())
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgShaPassedWithConflict);
            }
        }
        else if (!sha.has_value())
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgMissingOption, msg::option = "sha512");
        }

        if (auto p = sha.get())
        {
            if (!is_sha512(*p))
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgImproperShaLength, msg::value = *p);
            }
        }

        return sha;
    }

    void command_download_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        // Note that we must NOT make a VcpkgPaths because that will chdir
        auto parsed = args.parse_arguments(CommandDownloadMetadata);
        auto asset_cache_settings =
            parse_download_configuration(args.asset_sources_template()).value_or_exit(VCPKG_LINE_INFO);

        const Path file = parsed.command_arguments[0];
        const StringView display_path = file.is_absolute() ? file.filename() : file.native();
        auto sha = get_sha512_check(parsed);

        // Is this a store command?
        if (Util::Sets::contains(parsed.switches, SwitchStore))
        {
            auto hash = sha.get();
            if (!hash)
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgStoreOptionMissingSha);
            }

            auto s = fs.status(file, VCPKG_LINE_INFO);
            if (s != FileType::regular)
            {
                msg::println_error(msgIrregularFile, msg::path = file);
                Checks::unreachable(VCPKG_LINE_INFO);
            }
            auto actual_hash = Hash::get_file_hash(fs, file, Hash::Algorithm::Sha512).value_or_exit(VCPKG_LINE_INFO);
            if (!Strings::case_insensitive_ascii_equals(*hash, actual_hash))
            {
                msg::println_error(msgMismatchedFiles);
                Checks::unreachable(VCPKG_LINE_INFO);
            }

            if (!store_to_asset_cache(console_diagnostic_context, asset_cache_settings, file, actual_hash))
            {
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            Checks::exit_success(VCPKG_LINE_INFO);
        }
        else
        {
            // Try to fetch from urls
            auto it_headers = parsed.multisettings.find(SwitchHeader);
            View<std::string> headers;
            if (it_headers != parsed.multisettings.end())
            {
                headers = it_headers->second;
            }

            auto it_urls = parsed.multisettings.find(SwitchUrl);
            View<std::string> urls{};
            if (it_urls != parsed.multisettings.end())
            {
                urls = it_urls->second;
            }

            if (download_file_asset_cached(
                    console_diagnostic_context,
                    Util::Sets::contains(parsed.switches, SwitchZMachineReadableProgress) ? out_sink : null_sink,
                    asset_cache_settings,
                    fs,
                    urls,
                    headers,
                    file,
                    display_path,
                    sha))
            {
                Checks::exit_success(VCPKG_LINE_INFO);
            }

            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }
} // namespace vcpkg
