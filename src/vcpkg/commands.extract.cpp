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

    static int get_strip_count(const ParsedArguments& options)
    {
        auto iter = options.settings.find(OPTION_STRIP);
        if (iter != options.settings.end())
        {
            std::string value = iter->second;
            try
            {
                return std::stoi(value);
            }
            catch (std::exception&)
            {
                msg::println_error(msgErrorInvalidOption, msg::option = OPTION_STRIP, msg::value = value);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }
        // No --strip set, default to 0
        return 0;
    }

    std::vector<std::pair<Path, Path>> strip_map(const ExtractedArchive& archive, int num_leading_dir)
    {
        std::vector<std::pair<Path, Path>> result;

        const auto temp_dir = archive.temp_path;
        const auto base_path = archive.base_path;
        const auto proximate = archive.proximate_to_temp;

        for (const auto& prox_path : proximate)
        {
            auto old_path = temp_dir / Path{prox_path};

            auto prox_str = prox_path.native();

            for (int i = 0; i < num_leading_dir; ++i)
            {
                size_t pos = prox_str.find_first_of(VCPKG_PREFERRED_SEPARATOR);
                if (pos != std::string::npos)
                {
                    prox_str = prox_str.substr(pos + 1);
                }
                else
                {
                    prox_str = "";
                    break;
                }
            }

            Path new_path = prox_str.empty() ? "" : Path{base_path} / Path{prox_str};

            result.emplace_back(std::move(old_path), std::move(new_path));
        }
        return result;
    }

    static void extract_and_strip(const Filesystem& fs,
                                  const VcpkgPaths& paths,
                                  int num_leading_dirs_to_strip,
                                  Path archive_path,
                                  Path output_dir)
    {
        auto temp_dir =
            extract_archive_to_temp_subdirectory(fs, paths.get_tool_cache(), null_sink, archive_path, output_dir);

        ExtractedArchive archive = {
            temp_dir, output_dir, fs.get_regular_files_recursive_lexically_proximate(temp_dir, VCPKG_LINE_INFO)};

        auto mapping = strip_map(archive, num_leading_dirs_to_strip);

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
        auto strip_count = get_strip_count(parse_args);

        if (!fs.is_directory(destination_path))
        {
            fs.create_directories(destination_path, VCPKG_LINE_INFO);
        }

        if (strip_count > 0)
        {
            extract_and_strip(fs, paths, strip_count, archive_path, destination_path);
        }
        else
        {
            extract_archive(fs, paths.get_tool_cache(), null_sink, archive_path, destination_path);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
