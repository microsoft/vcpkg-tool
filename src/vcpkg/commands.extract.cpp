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
    static constexpr StringLiteral OPTION_EXTRACT_TYPE = "extract-type";

    static constexpr std::array<CommandSetting, 2> EXTRACT_SETTINGS = {{
        {OPTION_STRIP, []() { return msg::format(msgStripOption, msg::option = "strip"); }},
        {OPTION_EXTRACT_TYPE, []() { return msg::format(msgExtractTypeOption, msg::option = "extract-type"); }},
    }};

    const CommandStructure ExtractCommandStructure = {
        [] { return msg::format(msgExtractHelp); },
        2,
        3,
        {{}, {EXTRACT_SETTINGS}, {}},
        nullptr,
    };

    static ExtractionType get_extraction_type(const ParsedArguments& options)
    {
        static const std::unordered_map<std::string, ExtractionType> extraction_type_map = {
            {"tar", ExtractionType::TAR},
            {"zip", ExtractionType::ZIP},
            {"nupkg", ExtractionType::NUPKG},
            {"msi", ExtractionType::MSI},
            {"exe", ExtractionType::EXE},
        };

        auto iter = options.settings.find(OPTION_EXTRACT_TYPE);
        if (iter != options.settings.end())
        {
            std::string extraction_type = Strings::ascii_to_lowercase(iter->second);
            auto map_iter = extraction_type_map.find(extraction_type);
            if (map_iter != extraction_type_map.end())
            {
                return map_iter->second;
            }
            else
            {
                msg::println_error(msgErrorInvalidExtractTypeOption, msg::option = OPTION_EXTRACT_TYPE);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }
        return ExtractionType::UNKNOWN;
    }

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

        const auto base_path = archive.base_path;
        const auto proximate = archive.proximate;

#if defined(_WIN32)
        auto& delimiter = "\\";
#else

        auto& delimiter = "/";
#endif

        for (const auto& file_path : proximate)
        {
            auto old_path = base_path / Path{file_path};

            auto path_str = file_path.native();

            for (int i = 0; i < num_leading_dir; ++i)
            {
                size_t pos = path_str.find(delimiter);
                if (pos != std::string::npos)
                {
                    path_str = path_str.substr(pos + 1);
                }
            }

            // Trim the .partial.Xxx from the new path
            Path new_path;

            auto base_str = base_path.native();
            size_t pos = base_str.find(".partial");

            if (pos != std::string::npos)
            {
                new_path = Path{base_str.substr(0, pos)} / Path{path_str};
            }
            else
            {
                new_path = Path{base_str} / Path{path_str};
            }

            result.push_back({old_path, new_path});
        }
        return result;
    }

    static void extract_and_strip(const Filesystem& fs,
                                  const VcpkgPaths& paths,
                                  int strip_count,
                                  Path archive_path,
                                  Path destination_path,
                                  ExtractionType& extraction_type)
    {
        auto temp_dir = extract_archive_to_temp_subdirectory(
            fs, paths.get_tool_cache(), null_sink, archive_path, destination_path, extraction_type);

        ExtractedArchive archive = {temp_dir,
                                    fs.get_regular_files_recursive_lexically_proximate(temp_dir, VCPKG_LINE_INFO)};

        auto mapping = strip_map(archive, strip_count);

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

    void command_extract_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();
        auto parse_args = args.parse_arguments(ExtractCommandStructure);
        auto archive_path = Path{parse_args.command_arguments[0]};
        auto destination_path = Path{parse_args.command_arguments[1]};
        auto strip_count = get_strip_count(parse_args);
        auto extraction_type = get_extraction_type(parse_args);

        if (!fs.is_directory(destination_path))
        {
            fs.create_directories(destination_path, VCPKG_LINE_INFO);
        }

        if (strip_count > 0)
        {
            extract_and_strip(fs, paths, strip_count, archive_path, destination_path, extraction_type);
        }
        else
        {
            extract_archive(fs, paths.get_tool_cache(), null_sink, archive_path, destination_path, extraction_type);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}