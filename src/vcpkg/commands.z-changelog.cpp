#include <vcpkg/base/files.h>
#include <vcpkg/base/fmt.h>

#include <vcpkg/commands.portsdiff.h>
#include <vcpkg/commands.z-changelog.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <iterator>
#include <string>

namespace vcpkg
{
    constexpr CommandMetadata CommandZChangelogMetadata{
        "z-changelog",
        "Generate github.com/microsoft/vcpkg changelog",
        {},
        Undocumented,
        AutocompletePriority::Never,
        1,
        1,
        {},
        nullptr,
    };

    void command_z_changelog_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const auto parsed = args.parse_arguments(CommandZChangelogMetadata);
        const StringView git_commit_id_for_previous_snapshot = parsed.command_arguments[0];
        const auto maybe_portsdiff =
            find_portsdiff(console_diagnostic_context, paths, git_commit_id_for_previous_snapshot, "HEAD");
        const auto portsdiff = maybe_portsdiff.get();
        if (!portsdiff)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        std::string result;
        auto total_port_count = paths.get_filesystem()
                                    .get_directories_non_recursive(paths.builtin_ports_directory(), VCPKG_LINE_INFO)
                                    .size();
        fmt::format_to(std::back_inserter(result), "#### Total port count: {}\n", total_port_count);
        result.append("#### Total port count per triplet (tested): LINK\n");
        result.append("|triplet|ports available|\n");
        result.append("|---|---|\n");
        result.append("|x86-windows|Building...|\n");
        result.append("|**x64-windows**|Building...|\n");
        result.append("|x64-windows-release|Building...|\n");
        result.append("|x64-windows-static|Building...|\n");
        result.append("|x64-windows-static-md|Building...|\n");
        result.append("|x64-uwp|Building...|\n");
        result.append("|arm64-windows|Building...|\n");
        result.append("|arm64-windows-static-md|Building...|\n");
        result.append("|arm64-uwp|Building...|\n");
        result.append("|x64-osx|Building...|\n");
        result.append("|**arm64-osx**|Building...|\n");
        result.append("|**x64-linux**|Building...|\n");
        result.append("|arm-neon-android|Building...|\n");
        result.append("|x64-android|Building...|\n");
        result.append("|arm64-android|Building...|\n");
        result.append("\n");

        result.append("The following vcpkg-tool releases have occurred since the last registry release:\n");
        result.append("* \n");
        result.append("\n");

        result.append("In those tool releases, the following changes are particularly meaningful:\n");
        result.append("* \n");
        result.append("\n");

        if (!portsdiff->added_ports.empty())
        {
            result.append("<details>\n");
            fmt::format_to(std::back_inserter(result),
                           "<summary><b>The following {} ports have been added:</b></summary>\n\n",
                           portsdiff->added_ports.size());
            result.append("|port|version|\n");
            result.append("|---|---|\n");
            for (auto&& added_port : portsdiff->added_ports)
            {
                fmt::format_to(std::back_inserter(result), "|{}|{}|\n", added_port.port_name, added_port.version);
            }

            result.append("</details>\n\n");
        }

        if (!portsdiff->updated_ports.empty())
        {
            result.append("<details>\n");
            fmt::format_to(std::back_inserter(result),
                           "<summary><b>The following {} ports have been updated:</b></summary>\n\n",
                           portsdiff->updated_ports.size());
            result.append("|port|original version|new version|\n");
            result.append("|---|---|---|\n");
            for (auto&& updated_port : portsdiff->updated_ports)
            {
                fmt::format_to(std::back_inserter(result),
                               "|{}|{}|{}|\n",
                               updated_port.port_name,
                               updated_port.version_diff.left,
                               updated_port.version_diff.right);
            }

            result.append("</details>\n\n");
        }

        if (!portsdiff->removed_ports.empty())
        {
            result.append("<details>\n");
            fmt::format_to(std::back_inserter(result),
                           "<summary><b>The following {} ports have been removed:</b></summary>\n\n",
                           portsdiff->removed_ports.size());
            result.append("|port|\n");
            result.append("|---|\n");
            for (auto&& removed_port : portsdiff->removed_ports)
            {
                fmt::format_to(std::back_inserter(result), "|{}|\n", removed_port);
            }

            result.append("</details>\n\n");
        }

        result.append("#### New Contributors\n");

        msg::write_unlocalized_text(Color::none, result);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
