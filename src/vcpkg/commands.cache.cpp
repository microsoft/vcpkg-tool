#include <vcpkg/base/files.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/binaryparagraph.h>
#include <vcpkg/commands.cache.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands::Cache
{
    static std::vector<BinaryParagraph> read_all_binary_paragraphs(const VcpkgPaths& paths)
    {
        std::vector<BinaryParagraph> output;
        for (auto&& path : paths.get_filesystem().get_files_non_recursive(paths.packages(), VCPKG_LINE_INFO))
        {
            const auto pghs = Paragraphs::get_single_paragraph(paths.get_filesystem(), path / "CONTROL");
            if (const auto p = pghs.get())
            {
                const BinaryParagraph binary_paragraph = BinaryParagraph(*p);
                output.push_back(binary_paragraph);
            }
        }

        return output;
    }

    const CommandStructure COMMAND_STRUCTURE = {
        [] { return msg::format(msgCacheHelp).append_raw('\n').append(create_example_string("cache png")); },
        0,
        1,
        {},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed = args.parse_arguments(COMMAND_STRUCTURE);

        const std::vector<BinaryParagraph> binary_paragraphs = read_all_binary_paragraphs(paths);
        if (binary_paragraphs.empty())
        {
            msg::println(msgNoCachedPackages);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        if (parsed.command_arguments.empty())
        {
            for (const BinaryParagraph& binary_paragraph : binary_paragraphs)
            {
                msg::write_unlocalized_text_to_stdout(Color::none, binary_paragraph.displayname() + '\n');
            }
        }
        else
        {
            // At this point there is 1 argument
            for (const BinaryParagraph& binary_paragraph : binary_paragraphs)
            {
                const std::string displayname = binary_paragraph.displayname();
                if (!Strings::case_insensitive_ascii_contains(displayname, parsed.command_arguments[0]))
                {
                    continue;
                }
                msg::write_unlocalized_text_to_stdout(Color::none, displayname + '\n');
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
