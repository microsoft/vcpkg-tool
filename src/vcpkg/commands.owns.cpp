#include <vcpkg/commands.owns.h>
#include <vcpkg/help.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands::Owns
{
    static void search_file(Filesystem& fs,
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
                    msg::write_unlocalized_text_to_stdout(Color::none,
                                                          fmt::format("{}: {}\n", pgh.package.displayname(), file));
                }
            }
        }
    }
    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("owns zlib.dll"); },
        1,
        1,
        {},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        (void)args.parse_arguments(COMMAND_STRUCTURE);

        const StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());
        search_file(paths.get_filesystem(), paths.installed(), args.command_arguments[0], status_db);
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void OwnsCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Owns::perform_and_exit(args, paths);
    }
}
