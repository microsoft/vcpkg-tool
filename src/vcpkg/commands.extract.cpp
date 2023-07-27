#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.extract.h>
#include <vcpkg/commands.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands
{
    static constexpr StringLiteral OPTION_STRIP = "strip";

    static constexpr std::array<CommandSetting, 1> EXTRACT_SETTINGS = {{
        {OPTION_STRIP, []() { return msg::format(msgStripOption, msg::option = "strip"); }},
    }};

    const CommandStructure ExtractCommandStructure = {
        [] { return msg::format(msgExtractHelp); },
        2,
        3,
        {{}, {EXTRACT_SETTINGS}, {}},
        nullptr,
    };

    static StripSetting get_strip_setting(const ParsedArguments& options)
    {
        auto iter = options.settings.find(OPTION_STRIP);
        if (iter != options.settings.end())
        {
            std::string maybe_value = iter->second;

            if (Strings::case_insensitive_ascii_equals(maybe_value, "auto"))
            {
                return {StripMode::Automatic, -1};
            }

            auto maybe_strip_value = Strings::strto<int>(maybe_value);
            if (auto value = maybe_strip_value.get(); value && *value >= 0)
            {
                return {StripMode::Manual, *value};
            }

            // If the value is not an integer or is less than 0
            msg::println_error(msgErrorInvalidExtractOption, msg::option = OPTION_STRIP, msg::value = maybe_value);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        // No --strip set, default to 0
        return {StripMode::Manual, 0};
    }

    constexpr IsSlash is_slash;
    size_t get_common_directories_count(std::vector<Path> paths)
    {
        if (paths.size() == 0)
        {
            return 0;
        }

        std::string known_common_prefix = paths[0].native();
        for (size_t idx = 1; idx < paths.size(); ++idx)
        {
            auto&& candidate = paths[idx].native();
            auto mismatch_point = std::mismatch(known_common_prefix.begin(),
                                                known_common_prefix.end(),

                                                candidate.begin(),
                                                candidate.end())
                                      .first;
            known_common_prefix.erase(mismatch_point, known_common_prefix.end());
        }

        return std::count_if(known_common_prefix.begin(), known_common_prefix.end(), is_slash);
    }

    std::vector<std::pair<Path, Path>> get_archive_deploy_operations(const ExtractedArchive& archive,
                                                                     StripSetting strip_setting)
    {
        std::vector<std::pair<Path, Path>> result;

        const auto temp_dir = archive.temp_path;
        const auto base_path = archive.base_path;
        const auto proximate = archive.proximate_to_temp;

        size_t strip_count = strip_setting.mode == StripMode::Automatic ? get_common_directories_count(proximate)
                                                                        : static_cast<size_t>(strip_setting.count);

        for (const auto& prox_path : proximate)
        {
            auto old_path = temp_dir / Path{prox_path};

            auto prox_str = prox_path.native();
            auto first = prox_str.data();
            auto last = first + prox_str.size();

            // strip leading directories equivalent to the number specified
            for (size_t i = 0; i < strip_count && first != last; ++i)
            {
                first = std::find_if(first, last, is_slash);
                first = std::find_if_not(first, last, is_slash);
            }

            prox_str = std::string(first, static_cast<size_t>(last - first));

            Path new_path = prox_str.empty() ? "" : Path{base_path} / Path{prox_str};

            result.emplace_back(std::move(old_path), std::move(new_path));
        }

        return result;
    }

    static void extract_and_strip(
        const Filesystem& fs, const VcpkgPaths& paths, StripSetting strip_setting, Path archive_path, Path output_dir)
    {
        auto temp_dir =
            extract_archive_to_temp_subdirectory(fs, paths.get_tool_cache(), null_sink, archive_path, output_dir);

        ExtractedArchive archive = {
            temp_dir, output_dir, fs.get_regular_files_recursive_lexically_proximate(temp_dir, VCPKG_LINE_INFO)};

        auto mapping = get_archive_deploy_operations(archive, strip_setting);

        for (const auto& file : mapping)
        {
            const auto& source = file.first;
            const auto& destination = file.second;

            if (!destination.empty() && !fs.is_directory(destination.parent_path()))
            {
                fs.create_directories(destination.parent_path(), VCPKG_LINE_INFO);
            }

            if (!destination.empty())
            {
                fs.rename(source, destination, VCPKG_LINE_INFO);
            }
        }

        fs.remove_all(temp_dir, VCPKG_LINE_INFO);
    }

    void command_extract_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();
        auto parse_args = args.parse_arguments(ExtractCommandStructure);
        auto archive_path = Path{parse_args.command_arguments[0]};
        auto destination_path = Path{parse_args.command_arguments[1]};
        auto strip_setting = get_strip_setting(parse_args);

        if (!fs.is_directory(destination_path))
        {
            fs.create_directories(destination_path, VCPKG_LINE_INFO);
        }

        if (strip_setting.mode == StripMode::Manual && strip_setting.count == 0)
        {
            extract_archive(fs, paths.get_tool_cache(), null_sink, archive_path, destination_path);
        }
        else
        {
            extract_and_strip(fs, paths, strip_setting, archive_path, destination_path);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
