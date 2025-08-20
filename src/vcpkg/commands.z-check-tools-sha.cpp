#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.z-check-tools-sha.h>
#include <vcpkg/tools.test.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{

    static constexpr CommandSwitch CHECK_TOOL_SWITCHES[] = {
        {SwitchFix, msgCmdCheckToolsShaSwitchFix},
    };

    static constexpr CommandSetting CHECK_TOOL_SETTINGS[] = {
        {SwitchOnlyWithName, msgCmdCheckToolsShaSwitchOnlyWithName},
    };

    constexpr CommandMetadata CommandCheckToolsShaMetadata{
        "z-check-tools-sha",
        msgCmdCheckToolsShaSynopsis,
        {"vcpkg z-check-tools-sha scripts/vcpkg-tools.json"},
        Undocumented,
        AutocompletePriority::Internal,
        1,
        1,
        {CHECK_TOOL_SWITCHES, CHECK_TOOL_SETTINGS},
        nullptr,
    };

    void command_check_tools_sha_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        const auto parsed = args.parse_arguments(CommandCheckToolsShaMetadata);

        const auto file_to_check = (fs.current_path(VCPKG_LINE_INFO) / parsed.command_arguments[0]).lexically_normal();

        const auto dest_path = fs.create_or_get_temp_directory(VCPKG_LINE_INFO);

        auto content = fs.read_contents(file_to_check, VCPKG_LINE_INFO);

        auto data = parse_tool_data(content, file_to_check).value_or_exit(VCPKG_LINE_INFO);

        auto only_name_iter = parsed.settings.find(SwitchOnlyWithName);

        std::unordered_map<std::string, std::string> url_to_sha;

        std::vector<std::pair<std::string, Path>> urlAndPaths;
        for (auto& entry : data)
        {
            if (entry.url.empty()) continue;
            if (only_name_iter != parsed.settings.end())
            {
                if (entry.tool != only_name_iter->second)
                {
                    continue;
                }
            }
            auto iter = url_to_sha.emplace(entry.url, entry.sha512);

            Checks::check_exit(VCPKG_LINE_INFO, iter.first->second == entry.sha512);
            if (iter.second)
            {
                urlAndPaths.emplace_back(entry.url, dest_path / entry.archiveName + " - " + entry.sha512.substr(0, 10));
            }
        }
        if (urlAndPaths.empty())
        {
            Checks::msg_exit_with_message(
                VCPKG_LINE_INFO,
                msg::format(msgNoEntryWithName,
                            msg::value = (only_name_iter != parsed.settings.end() ? only_name_iter->second : "*")));
        }

        msg::println(msgDownloadingTools, msg::count = urlAndPaths.size());
        auto result = download_files_no_cache(console_diagnostic_context, urlAndPaths, {}, {});

        std::unordered_map<std::string, std::string> url_to_fixed_sha;
        auto http_codes_iter = result.begin();
        bool has_http_error = false;
        bool has_sha_error = false;
        for (auto& urlAndPath : urlAndPaths)
        {
            if (*http_codes_iter == 200)
            {
                auto sha =
                    Hash::get_file_hash(fs, urlAndPath.second, Hash::Algorithm::Sha512).value_or_exit(VCPKG_LINE_INFO);
                if (url_to_sha[urlAndPath.first] != sha)
                {
                    msg::println(msgDownloadFailedHashMismatch, msg::url = urlAndPath.first);
                    msg::println(msgDownloadFailedHashMismatchExpectedHash, msg::sha = url_to_sha[urlAndPath.first]);
                    msg::println(msgDownloadFailedHashMismatchActualHash, msg::sha = sha);
                    msg::println();
                    has_sha_error = true;
                    url_to_fixed_sha[urlAndPath.first] = sha;
                }
            }
            else
            {
                msg::println(msgDownloadFailedStatusCode, msg::url = urlAndPath.first, msg::value = *http_codes_iter);
                has_http_error = true;
            }
            fs.remove(urlAndPath.second, VCPKG_LINE_INFO);
            ++http_codes_iter;
        }

        if (!has_sha_error)
        {
            msg::println(msgAllShasValid);
        }

        if (!url_to_fixed_sha.empty() && Util::Sets::contains(parsed.switches, SwitchFix))
        {
            Json::Object as_object = Json::parse_object(content, file_to_check).value_or_exit(VCPKG_LINE_INFO);
            int fixed = 0;
            for (auto&& entry : as_object["tools"].array(VCPKG_LINE_INFO))
            {
                auto maybe_url = entry.object(VCPKG_LINE_INFO).get("url");
                if (maybe_url)
                {
                    auto iter = url_to_fixed_sha.find(maybe_url->string(VCPKG_LINE_INFO).to_string());
                    if (iter != url_to_fixed_sha.end())
                    {
                        entry.object(VCPKG_LINE_INFO).insert_or_replace("sha512", iter->second);
                        ++fixed;
                    }
                }
            }

            fs.write_contents(file_to_check, Json::stringify(as_object), VCPKG_LINE_INFO);
            msg::println(msgFixedEntriesInFile, msg::count = fixed, msg::path = file_to_check);
            has_sha_error = false;
        }

        Checks::exit_with_code(VCPKG_LINE_INFO, (has_sha_error || has_http_error) ? EXIT_FAILURE : EXIT_SUCCESS);
    }
}
