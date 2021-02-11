#include <vcpkg/base/downloads.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.xdownload.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands::X_Download
{
    static constexpr StringLiteral OPTION_STORE = "store";
    static constexpr StringLiteral OPTION_URL = "url";
    static constexpr StringLiteral OPTION_HEADER = "header";

    static constexpr CommandSwitch FETCH_SWITCHES[] = {
        {OPTION_STORE, "Indicates the file should be stored instead of fetched"},
    };
    static constexpr CommandMultiSetting FETCH_MULTISETTINGS[] = {
        {OPTION_URL, "URL to download and store if missing from cache"},
        {OPTION_HEADER, "(Not Implemented) Additional header to use when fetching from URLs"},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        Strings::format("The argument must be at least a file path and a SHA512\n%s",
                        create_example_string("x-download <filepath> <sha512> [--url=https://...]...")),
        2,
        2,
        {{FETCH_SWITCHES}, {}, FETCH_MULTISETTINGS},
        nullptr,
    };

    static bool is_lower_hex(StringView sha)
    {
        return std::all_of(
            sha.begin(), sha.end(), [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
    }
    static bool is_lower_sha512(StringView sha) { return sha.size() == 128 && is_lower_hex(sha); }

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed = args.parse_arguments(COMMAND_STRUCTURE);
        fs::path file = Files::combine(paths.original_cwd, fs::u8path(args.command_arguments[0]));
        file.make_preferred();

        std::string sha = Strings::ascii_to_lowercase(std::string(args.command_arguments[1]));
        if (!is_lower_sha512(sha))
        {
            Checks::exit_with_message(
                VCPKG_LINE_INFO, "Error: SHA512's must be 128 hex characters: '%s'", args.command_arguments[1]);
        }

        auto& fs = paths.get_filesystem();

        // Is this a store command?
        if (Util::Sets::contains(parsed.switches, OPTION_STORE))
        {
            auto s = fs.status(VCPKG_LINE_INFO, file);
            if (s.type() != fs::file_type::regular)
            {
                Checks::exit_with_message(
                    VCPKG_LINE_INFO, "Error: path was not a regular file: %s", fs::u8string(file));
            }
            auto hash =
                Strings::ascii_to_lowercase(Hash::get_file_hash(VCPKG_LINE_INFO, fs, file, Hash::Algorithm::Sha512));
            if (hash != sha) Checks::exit_with_message(VCPKG_LINE_INFO, "Error: file to store does not match hash");
            paths.get_download_manager().put_file_to_mirror(fs, file, sha);
            Checks::exit_success(VCPKG_LINE_INFO);
        }
        else
        {
            // Try to fetch from urls
            auto it_urls = parsed.multisettings.find(OPTION_URL);
            if (it_urls == parsed.multisettings.end())
            {
                paths.get_download_manager().download_file(fs, View<std::string>{}, file, sha);
            }
            else
            {
                paths.get_download_manager().download_file(fs, it_urls->second, file, sha);
            }
            Checks::exit_success(VCPKG_LINE_INFO);
        }
    }

    void XDownloadCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        X_Download::perform_and_exit(args, paths);
    }
}
