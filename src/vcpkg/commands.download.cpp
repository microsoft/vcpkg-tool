#include <vcpkg/base/fwd/message_sinks.h>

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
    constexpr StringLiteral OPTION_STORE = "store";
    constexpr StringLiteral OPTION_SKIP_SHA512 = "skip-sha512";
    constexpr StringLiteral OPTION_SHA512 = "sha512";
    constexpr StringLiteral OPTION_URL = "url";
    constexpr StringLiteral OPTION_HEADER = "header";
    constexpr StringLiteral OPTION_MACHINE_PROGRESS = "z-machine-readable-progress";

    constexpr CommandSwitch FETCH_SWITCHES[] = {
        {OPTION_STORE, msgCmdXDownloadOptStore},
        {OPTION_SKIP_SHA512, msgCmdXDownloadOptSkipSha},
        {OPTION_MACHINE_PROGRESS, {}},
    };
    constexpr CommandSetting FETCH_SETTINGS[] = {
        {OPTION_SHA512, msgCmdXDownloadOptSha},
    };
    constexpr CommandMultiSetting FETCH_MULTISETTINGS[] = {
        {OPTION_URL, msgCmdXDownloadOptUrl},
        {OPTION_HEADER, msgCmdXDownloadOptHeader},
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
        Optional<std::string> sha = nullopt;
        auto sha_it = parsed.settings.find(OPTION_SHA512);
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

        if (Util::Sets::contains(parsed.switches, OPTION_SKIP_SHA512))
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
            Strings::inplace_ascii_to_lowercase(p->data(), p->data() + p->size());
        }

        return sha;
    }

    void command_download_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        auto parsed = args.parse_arguments(CommandDownloadMetadata);
        DownloadManager download_manager{
            parse_download_configuration(args.asset_sources_template()).value_or_exit(VCPKG_LINE_INFO)};
        auto file = fs.absolute(parsed.command_arguments[0], VCPKG_LINE_INFO);

        auto sha = get_sha512_check(parsed);

        // Is this a store command?
        if (Util::Sets::contains(parsed.switches, OPTION_STORE))
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
            download_manager.put_file_to_mirror(fs, file, actual_hash).value_or_exit(VCPKG_LINE_INFO);
            Checks::exit_success(VCPKG_LINE_INFO);
        }
        else
        {
            // Try to fetch from urls
            auto it_headers = parsed.multisettings.find(OPTION_HEADER);
            View<std::string> headers;
            if (it_headers != parsed.multisettings.end())
            {
                headers = it_headers->second;
            }

            auto it_urls = parsed.multisettings.find(OPTION_URL);
            View<std::string> urls{};
            if (it_urls != parsed.multisettings.end())
            {
                urls = it_urls->second;
            }

            download_manager.download_file(fs,
                                           urls,
                                           headers,
                                           file,
                                           sha,
                                           Util::Sets::contains(parsed.switches, OPTION_MACHINE_PROGRESS) ? stdout_sink
                                                                                                          : null_sink);
            Checks::exit_success(VCPKG_LINE_INFO);
        }
    }
} // namespace vcpkg
