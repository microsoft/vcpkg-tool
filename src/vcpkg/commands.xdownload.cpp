#include <vcpkg/base/downloads.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/commands.xdownload.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::X_Download
{
    static constexpr StringLiteral OPTION_STORE = "store";
    static constexpr StringLiteral OPTION_SKIP_SHA512 = "skip-sha512";
    static constexpr StringLiteral OPTION_SHA512 = "sha512";
    static constexpr StringLiteral OPTION_URL = "url";
    static constexpr StringLiteral OPTION_HEADER = "header";

    static constexpr CommandSwitch FETCH_SWITCHES[] = {
        {OPTION_STORE, "Indicates the file should be stored instead of fetched"},
        {OPTION_SKIP_SHA512, "Do not check the SHA512 of the downloaded file"},
    };
    static constexpr CommandSetting FETCH_SETTINGS[] = {
        {OPTION_SHA512, "The hash of the file to be downloaded"},
    };
    static constexpr CommandMultiSetting FETCH_MULTISETTINGS[] = {
        {OPTION_URL, "URL to download and store if missing from cache"},
        {OPTION_HEADER, "Additional header to use when fetching from URLs"},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        Strings::format("%s\n%s",
                        create_example_string("x-download <filepath> [--sha512=]<sha512> [--url=https://...]..."),
                        create_example_string("x-download <filepath> --skip-sha512 [--url=https://...]...")),
        1,
        2,
        {FETCH_SWITCHES, FETCH_SETTINGS, FETCH_MULTISETTINGS},
        nullptr,
    };

    static bool is_hex(StringView sha) { return std::all_of(sha.begin(), sha.end(), ParserBase::is_hex_digit); }
    static bool is_sha512(StringView sha) { return sha.size() == 128 && is_hex(sha); }

    static Optional<std::string> get_sha512_check(const VcpkgCmdArguments& args, const ParsedArguments& parsed)
    {
        Optional<std::string> sha = nullopt;
        auto sha_it = parsed.settings.find(OPTION_SHA512);
        if (args.command_arguments.size() > 1)
        {
            if (sha_it != parsed.settings.end())
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgShaPassedAsArgAndOption);
            }
            sha = args.command_arguments[1];
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
            Strings::ascii_to_lowercase(p->data(), p->data() + p->size());
        }

        return sha;
    }

    void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs)
    {
        auto parsed = args.parse_arguments(COMMAND_STRUCTURE);
        DownloadManager download_manager{
            parse_download_configuration(args.asset_sources_template()).value_or_exit(VCPKG_LINE_INFO)};
        auto file = fs.absolute(args.command_arguments[0], VCPKG_LINE_INFO);

        auto sha = get_sha512_check(args, parsed);

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
                std::string github("https://github.com");
                std::string https("https://");
                for (std::string& s : it_urls->second)
                {
                    if (s.compare(0, github.size(), github) == 0)
                    {
                        s = https.append(args.github_mirror->c_str()).append(s.replace(0, 7, ""));
                        Debug::println("https.appen: ", s);
                    }
                }
                urls = it_urls->second;
            }

            download_manager.download_file(fs, urls, headers, file, sha);
            Checks::exit_success(VCPKG_LINE_INFO);
        }
    }

    void XDownloadCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        X_Download::perform_and_exit(args, fs);
    }
}
