#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.format-manifest.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    struct ToWrite
    {
        std::string original_source;
        std::unique_ptr<SourceControlFile> scf;
        Path control_path;
        Path file_to_write;
    };

    void open_for_write(const Filesystem& fs, const ToWrite& data)
    {
        const auto& original_path_string = data.control_path.native();
        const auto& file_to_write_string = data.file_to_write.native();
        bool in_place = data.file_to_write == original_path_string;
        if (in_place)
        {
            Debug::println("Formatting ", file_to_write_string);
        }
        else
        {
            Debug::println("Converting ", original_path_string, " -> ", file_to_write_string);
        }

        auto res = serialize_manifest(*data.scf);

        // reparse res to ensure no semantic changes were made
        auto maybe_reparsed =
            SourceControlFile::parse_project_manifest_object(StringLiteral{"<unsaved>"}, res, null_sink);
        bool reparse_matches;
        if (auto reparsed = maybe_reparsed.get())
        {
            reparse_matches = **reparsed == *data.scf;
        }
        else
        {
            // if we failed to reparse clearly it differs
            reparse_matches = false;
        }

        if (!reparse_matches)
        {
            Checks::msg_exit_maybe_upgrade(
                VCPKG_LINE_INFO,
                msg::format(msgMismatchedManifestAfterReserialize)
                    .append_raw(fmt::format("\n=== Original File ===\n{}\n=== Serialized File ===\n{}\n",
                                            data.original_source,
                                            Json::stringify(res, {}))));
        }

        fs.write_contents(data.file_to_write, Json::stringify(res), VCPKG_LINE_INFO);
        if (!in_place)
        {
            fs.remove(original_path_string, VCPKG_LINE_INFO);
        }
    }

    constexpr CommandSwitch FORMAT_SWITCHES[] = {
        {SwitchAll, msgCmdFormatManifestOptAll},
        {SwitchConvertControl, msgCmdFormatManifestOptConvertControl},
    };

} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandFormatManifestMetadata{
        "format-manifest",
        msgCmdFormatManifestSynopsis,
        {msgCmdFormatManifestExample1, "vcpkg format-manifest ports/zlib/vcpkg.json", "vcpkg format-manifest --all"},
        Undocumented,
        AutocompletePriority::Public,
        0,
        SIZE_MAX,
        {FORMAT_SWITCHES},
        nullptr,
    };

    void command_format_manifest_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(CommandFormatManifestMetadata);

        auto& fs = paths.get_filesystem();
        bool has_error = false;

        const bool format_all = Util::Sets::contains(parsed_args.switches, SwitchAll);
        const bool convert_control = Util::Sets::contains(parsed_args.switches, SwitchConvertControl);

        if (!format_all && convert_control)
        {
            msg::println_warning(msgMissingArgFormatManifest);
        }

        if (!format_all && parsed_args.command_arguments.empty())
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgFailedToFormatMissingFile);
        }

        std::vector<ToWrite> to_write;
        for (Path path : parsed_args.command_arguments)
        {
            if (path.is_relative())
            {
                path = paths.original_cwd / path;
            }

            auto maybe_contents = fs.try_read_contents(path);
            auto contents = maybe_contents.get();
            if (!contents)
            {
                has_error = true;
                msg::println(maybe_contents.error());
                continue;
            }

            if (path.filename() == "CONTROL")
            {
                auto maybe_control = Paragraphs::try_load_control_file_text(contents->content, contents->origin);
                if (auto control = maybe_control.get())
                {
                    to_write.push_back(ToWrite{contents->content,
                                               std::move(*control),
                                               std::move(path),
                                               Path(path.parent_path()) / "vcpkg.json"});
                }
                else
                {
                    has_error = true;
                    msg::println(maybe_control.error());
                }
            }
            else
            {
                auto maybe_manifest =
                    Paragraphs::try_load_project_manifest_text(contents->content, contents->origin, out_sink);
                if (auto manifest = maybe_manifest.get())
                {
                    to_write.push_back(ToWrite{contents->content, std::move(*manifest), path, path});
                }
                else
                {
                    has_error = true;
                    msg::println(maybe_manifest.error());
                }
            }
        }

        if (format_all)
        {
            for (const auto& dir : fs.get_directories_non_recursive(paths.builtin_ports_directory(), VCPKG_LINE_INFO))
            {
                auto maybe_manifest =
                    Paragraphs::try_load_builtin_port_required(fs, dir.filename(), paths.builtin_ports_directory());
                if (auto manifest = maybe_manifest.maybe_scfl.get())
                {
                    auto original = manifest->control_path;
                    if (original.filename() == "CONTROL")
                    {
                        if (convert_control)
                        {
                            to_write.push_back(ToWrite{maybe_manifest.on_disk_contents,
                                                       std::move(manifest->source_control_file),
                                                       original,
                                                       Path(original.parent_path()) / "vcpkg.json"});
                        }
                    }
                    else
                    {
                        to_write.push_back(ToWrite{maybe_manifest.on_disk_contents,
                                                   std::move(manifest->source_control_file),
                                                   original,
                                                   original});
                    }
                }
                else
                {
                    has_error = true;
                    msg::println(maybe_manifest.maybe_scfl.error());
                }
            }
        }

        for (auto const& el : to_write)
        {
            open_for_write(fs, el);
        }

        if (has_error)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        else
        {
            msg::println(msgManifestFormatCompleted);
            Checks::exit_success(VCPKG_LINE_INFO);
        }
    }
} // namespace vcpkg
