#include <vcpkg/base/files.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/binaryparagraph.h>
#include <vcpkg/commands.cache.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    std::vector<BinaryParagraph> read_all_binary_paragraphs(const VcpkgPaths& paths)
    {
        std::vector<BinaryParagraph> output;
        for (auto&& path : paths.get_filesystem().get_files_non_recursive(paths.packages(), VCPKG_LINE_INFO))
        {
            auto pghs = Paragraphs::get_single_paragraph(paths.get_filesystem(), std::move(path) / "CONTROL");
            if (const auto p = pghs.get())
            {
                output.emplace_back(std::move(*p));
            }
        }

        return output;
    }
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandCacheMetadata{
        "cache",
        msgCmdCacheSynopsis,
        {msgCmdCacheExample1, "vcpkg cache png"},
        AutocompletePriority::Public,
        0,
        1,
        {},
        nullptr,
    };

    void command_cache_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed = args.parse_arguments(CommandCacheMetadata);

        const std::vector<BinaryParagraph> binary_paragraphs = read_all_binary_paragraphs(paths);
        if (binary_paragraphs.empty())
        {
            msg::println(msgNoCachedPackages);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        std::string filter;
        if (!parsed.command_arguments.empty())
        {
            filter = parsed.command_arguments[0];
        }

        for (const BinaryParagraph& binary_paragraph : binary_paragraphs)
        {
            const std::string displayname = binary_paragraph.displayname();
            if (!Strings::case_insensitive_ascii_contains(displayname, filter))
            {
                continue;
            }

            msg::write_unlocalized_text_to_stdout(Color::none, displayname + '\n');
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
