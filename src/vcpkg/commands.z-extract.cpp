#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.z-extract.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    constexpr CommandSetting EXTRACT_SETTINGS[] = {
        {SwitchStrip, msgCmdZExtractOptStrip},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandZExtractMetadata{
        "z-extract",
        msgExtractHelp,
        {msgCmdZExtractExample1, msgCmdZExtractExample2},
        Undocumented,
        AutocompletePriority::Internal,
        2,
        3,
        {{}, {EXTRACT_SETTINGS}},
        nullptr,
    };

    ExpectedL<StripSetting> get_strip_setting(const std::map<StringLiteral, std::string, std::less<>>& settings)
    {
        auto iter = settings.find(SwitchStrip);
        if (iter == settings.end())
        {
            // no strip option specified - default to manual strip 0
            return StripSetting{StripMode::Manual, 0};
        }

        std::string maybe_value = iter->second;

        if (Strings::case_insensitive_ascii_equals(maybe_value, "auto"))
        {
            return StripSetting{StripMode::Automatic, -1};
        }

        auto maybe_strip_value = Strings::strto<int>(maybe_value);
        if (auto value = maybe_strip_value.get(); value && *value >= 0)
        {
            return StripSetting{StripMode::Manual, *value};
        }

        // If the value is not an integer or is less than 0
        return msg::format_error(msgErrorInvalidExtractOption, msg::option = SwitchStrip, msg::value = maybe_value);
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

        const auto& temp_dir = archive.temp_path;
        const auto& base_path = archive.base_path;
        const auto& proximate = archive.proximate_to_temp;

        size_t strip_count = strip_setting.mode == StripMode::Automatic ? get_common_directories_count(proximate)
                                                                        : static_cast<size_t>(strip_setting.count);

        for (const auto& prox_path : proximate)
        {
            const auto& prox_str = prox_path.native();
            auto first = prox_str.data();
            auto last = first + prox_str.size();

            // strip leading directories equivalent to the number specified
            for (size_t i = 0; i < strip_count && first != last; ++i)
            {
                first = std::find_if(first, last, is_slash);
                first = std::find_if_not(first, last, is_slash);
            }

            if (first == last)
            {
                continue;
            }

            result.emplace_back(temp_dir / prox_path,
                                base_path / std::string(first, static_cast<size_t>(last - first)));
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

            if (!fs.is_directory(destination.parent_path()))
            {
                fs.create_directories(destination.parent_path(), VCPKG_LINE_INFO);
            }

            fs.rename(source, destination, VCPKG_LINE_INFO);
        }

        fs.remove_all(temp_dir, VCPKG_LINE_INFO);
    }

    void command_z_extract_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();
        auto parse_args = args.parse_arguments(CommandZExtractMetadata);
        auto archive_path = Path{parse_args.command_arguments[0]};
        auto destination_path = Path{parse_args.command_arguments[1]};
        auto strip_setting = get_strip_setting(parse_args.settings).value_or_exit(VCPKG_LINE_INFO);

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
} // namespace vcpkg
