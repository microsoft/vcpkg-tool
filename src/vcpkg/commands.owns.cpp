#include <vcpkg/base/files.h>

#include <vcpkg/commands.owns.h>
#include <vcpkg/installeddatabase.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    void search_file(const ReadOnlyFilesystem& fs,
                     const InstalledPaths& installed,
                     const std::string& file_substr,
                     const StatusParagraphs& status_db)
    {
        const auto installed_files = get_installed_files(fs, installed, status_db);
        for (auto&& pgh_and_file : installed_files)
        {
            const StatusParagraph& pgh = pgh_and_file.pgh;

            for (const std::string& file : pgh_and_file.files)
            {
                if (file.find(file_substr) != std::string::npos)
                {
                    msg::write_unlocalized_text(Color::none, fmt::format("{}: {}\n", pgh.package.display_name(), file));
                }
            }
        }
    }
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandOwnsMetadata{
        "owns",
        msgHelpOwnsCommand,
        {msgCmdOwnsExample1, "vcpkg owns zlib1.dll"},
        Undocumented,
        AutocompletePriority::Public,
        1,
        1,
        {},
        nullptr,
    };

    void command_owns_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const auto parsed = args.parse_arguments(CommandOwnsMetadata);
        InstalledDatabaseLock installed_lock{
            paths.get_filesystem(), paths.installed(), args.wait_for_lock, args.ignore_lock_failures};
        const StatusParagraphs status_db = database_load(paths.get_filesystem(), paths.installed(), installed_lock);
        search_file(paths.get_filesystem(), paths.installed(), parsed.command_arguments[0], status_db);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
