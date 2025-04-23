#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>

#include <vcpkg/commands.check-tools-sha.h>
#include <vcpkg/tools.test.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include "vcpkg/base/downloads.h"

namespace vcpkg
{
    constexpr CommandMetadata CommandCheckToolsShaMetadata{
        "x-check-tools-sha",
        msgCmdCheckToolsShaSynopsis,
        {"vcpkg x-check-tools-sha scripts/vcpkg-tools.json"},
        Undocumented,
        AutocompletePriority::Internal,
        1,
        1,
        {},
        nullptr,
    };

    void command_check_tools_sha_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        const auto parsed = args.parse_arguments(CommandCheckToolsShaMetadata);

        const auto file_to_check = (fs.current_path(VCPKG_LINE_INFO) / parsed.command_arguments[0]).lexically_normal();

        const auto dest_path = fs.create_or_get_temp_directory(VCPKG_LINE_INFO);

        auto content = fs.read_contents(file_to_check, VCPKG_LINE_INFO);

        auto data = parse_tool_data(content, file_to_check).value_or_exit(VCPKG_LINE_INFO);

        std::unordered_map<std::string, std::string> seen_urls;

        std::vector<std::pair<std::string, Path>> urls;
        for (auto& entry : data)
        {
            if (entry.url.empty()) continue;
            auto iter = seen_urls.emplace(entry.url, entry.sha512);

            Checks::check_exit(VCPKG_LINE_INFO, iter.first->second == entry.sha512);
            if (iter.second)
            {
                urls.emplace_back(entry.url, dest_path / entry.archiveName + " - " + entry.sha512.substr(0, 10));
            }
        }

        fmt::println("Downloading {} tools", urls.size());
        auto result = download_files_no_cache(console_diagnostic_context, urls, {}, {});

        auto http_codes_iter = result.begin();
        for (auto& url : urls)
        {
            if (*http_codes_iter == 200)
            {
                auto sha = Hash::get_file_hash(fs, url.second, Hash::Algorithm::Sha512).value_or_exit(VCPKG_LINE_INFO);
                if (seen_urls[url.first] != sha)
                {
                    fmt::println("Error: Wrong sha for {}", url.first);
                }
            }
            fs.remove(url.second, VCPKG_LINE_INFO);
            ++http_codes_iter;
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
