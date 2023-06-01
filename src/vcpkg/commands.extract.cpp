#include <vcpkg/base/fwd/message_sinks.h>
#include <vcpkg/base/files.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.extract.h>
#include <vcpkg/commands.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands
{
    static constexpr StringLiteral OPTION_STRIP = "strip";

    constexpr std::array<CommandSetting, 1> EXTRACT_SETTINGS = {
        {{OPTION_STRIP, []() { return msg::format(msgStripOption); }},
        }};
    
    const CommandStructure ExtractCommandStructure = {
        [] { return msg::format(msgExtractHelp); },
        2,
        3,
        {{}, {EXTRACT_SETTINGS}, {}},
        nullptr,
    };

    int get_strip_count(const ParsedArguments& options)
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
                // todo: add error handling
                return 0;
            }
        }
        // No --strip set, default to 0
        return 0;
    }

    void extract_command_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();
        auto parse_args = args.parse_arguments(ExtractCommandStructure);
        auto archive_path = Path{parse_args.command_arguments[0]};
        auto destination_path = Path{parse_args.command_arguments[1]};
        auto strip_count = get_strip_count(parse_args);

        if (strip_count > 0)
        {
            auto temp = extract_archive_to_temp_subdirectory(
                fs, paths.get_tool_cache(), null_sink, archive_path, destination_path);

            auto mapping = strip_mapping(fs, destination_path, strip_count + 1);

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

            fs.remove_all(temp, VCPKG_LINE_INFO);
        }
        else
        {
            extract_archive(fs, paths.get_tool_cache(), null_sink, archive_path, destination_path);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}