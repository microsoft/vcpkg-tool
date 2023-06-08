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
        static constexpr StringLiteral EXTRACTION_TYPE_TAR = "tar";
        static constexpr StringLiteral EXTRACTION_TYPE_ZIP = "zip";
        static constexpr StringLiteral EXTRACTION_TYPE_NUPKG = "nupkg";
        static constexpr StringLiteral EXTRACTION_TYPE_MSI = "msi";

        auto iter = options.settings.find(OPTION_EXTRACT_TYPE);

        if (iter != options.settings.end())
        {
            std::string extraction_type = Strings::ascii_to_lowercase(iter->second);
            if (extraction_type == EXTRACTION_TYPE_TAR)
            {
                return ExtractionType::TAR;
            }
            else if (extraction_type == EXTRACTION_TYPE_ZIP)
            {
                return ExtractionType::ZIP;
            }
            else if (extraction_type == EXTRACTION_TYPE_NUPKG)
            {
                return ExtractionType::NUPKG;
            }
            else if (extraction_type == EXTRACTION_TYPE_MSI)
            {
                return ExtractionType::MSI;
            }
            else
            {
                msg::println_error(
                    msgErrorInvalidExtractTypeOption, msg::option = OPTION_EXTRACT_TYPE, msg::value = extraction_type);
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
                msg::println_error(msgErrorInvalidStripOption, msg::option = OPTION_STRIP, msg::value = value);
                Checks::exit_fail(VCPKG_LINE_INFO);
                return 0;
            }
        }
        // No --strip set, default to 0
        return 0;
    }

    std::vector<std::pair<Path, Path>> strip_map(const ExtractedArchive& archive, int num_leading_dir)
    {
        std::vector<std::pair<Path, Path>> result;

        auto base_path = archive.base_path;
        auto proximate = archive.proximate;

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
            auto new_path = base_path / Path{path_str};

            result.push_back({old_path, new_path});
        }
        return result;
    }

    static void extract_and_strip(Filesystem& fs,
                                  const VcpkgPaths& paths,
                                  int strip_count,
                                  Path archive_path,
                                  Path destination_path,
                                  ExtractionType& extraction_type)
    {
        auto temp_dir = extract_archive_to_temp_subdirectory(
            fs, paths.get_tool_cache(), null_sink, archive_path, destination_path, extraction_type);

        ExtractedArchive archive = {
            destination_path, fs.get_regular_files_recursive_lexically_proximate(destination_path, VCPKG_LINE_INFO)};

        auto mapping = strip_map(archive, strip_count + 1);

        for (auto&& file : mapping)
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

    void extract_command_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();
        auto parse_args = args.parse_arguments(ExtractCommandStructure);
        auto archive_path = Path{parse_args.command_arguments[0]};
        auto destination_path = Path{parse_args.command_arguments[1]};
        auto strip_count = get_strip_count(parse_args);
        auto extraction_type = get_extraction_type(parse_args);

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