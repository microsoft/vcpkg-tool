#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>

#include <vcpkg/commands.format-manifest.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    struct ToWrite
    {
        SourceControlFile scf;
        Path file_to_write;
        Path original_path;
        std::string original_source;
    };

    Optional<ToWrite> read_manifest(const ReadOnlyFilesystem& fs, Path&& manifest_path)
    {
        const auto& path_string = manifest_path.native();
        Debug::println("Reading ", path_string);
        auto contents = fs.read_contents(manifest_path, VCPKG_LINE_INFO);
        auto parsed_json_opt = Json::parse(contents, manifest_path);
        if (!parsed_json_opt)
        {
            msg::println(Color::error, LocalizedString::from_raw(parsed_json_opt.error()->to_string()));
            return nullopt;
        }

        const auto& parsed_json = parsed_json_opt.value(VCPKG_LINE_INFO).value;
        if (!parsed_json.is_object())
        {
            msg::println_error(msgJsonErrorMustBeAnObject, msg::path = path_string);
            return nullopt;
        }

        auto parsed_json_obj = parsed_json.object(VCPKG_LINE_INFO);

        auto scf = SourceControlFile::parse_project_manifest_object(path_string, parsed_json_obj, stdout_sink);
        if (!scf)
        {
            msg::println_error(msgFailedToParseManifest, msg::path = path_string);
            print_error_message(scf.error());
            msg::println();
            return nullopt;
        }

        return ToWrite{
            std::move(*scf.value(VCPKG_LINE_INFO)),
            manifest_path,
            manifest_path,
            std::move(contents),
        };
    }

    Optional<ToWrite> read_control_file(const ReadOnlyFilesystem& fs, Path&& control_path)
    {
        Debug::println("Reading ", control_path);

        auto manifest_path = Path(control_path.parent_path()) / "vcpkg.json";
        auto contents = fs.read_contents(control_path, VCPKG_LINE_INFO);
        auto paragraphs = Paragraphs::parse_paragraphs(contents, control_path);

        if (!paragraphs)
        {
            msg::println_error(msg::format(msgFailedToReadParagraph, msg::path = control_path)
                                   .append_raw(": ")
                                   .append_raw(paragraphs.error()));
            return {};
        }
        auto scf_res =
            SourceControlFile::parse_control_file(control_path, std::move(paragraphs).value(VCPKG_LINE_INFO));
        if (!scf_res)
        {
            msg::println_error(msgFailedToParseControl, msg::path = control_path);
            print_error_message(scf_res.error());
            return {};
        }

        return ToWrite{
            std::move(*scf_res.value(VCPKG_LINE_INFO)),
            manifest_path,
            control_path,
            std::move(contents),
        };
    }

    void open_for_write(const Filesystem& fs, const ToWrite& data)
    {
        const auto& original_path_string = data.original_path.native();
        const auto& file_to_write_string = data.file_to_write.native();
        if (data.file_to_write == data.original_path)
        {
            Debug::println("Formatting ", file_to_write_string);
        }
        else
        {
            Debug::println("Converting ", file_to_write_string, " -> ", original_path_string);
        }

        auto res = serialize_manifest(data.scf);

        // reparse res to ensure no semantic changes were made
        auto maybe_reparsed = SourceControlFile::parse_project_manifest_object(StringView{}, res, null_sink);
        bool reparse_matches;
        if (auto reparsed = maybe_reparsed.get())
        {
            reparse_matches = **reparsed == data.scf;
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

        // the manifest scf is correct
        std::error_code ec;
        fs.write_contents(data.file_to_write, Json::stringify(res), ec);
        if (ec)
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                        msg::format(msgFailedToWriteManifest, msg::path = file_to_write_string)
                                            .append_raw(": ")
                                            .append_raw(ec.message()));
        }
        if (data.original_path != data.file_to_write)
        {
            fs.remove(data.original_path, ec);
            if (ec)
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                            msg::format(msgFailedToRemoveControl, msg::path = original_path_string)
                                                .append_raw(": ")
                                                .append_raw(ec.message()));
            }
        }
    }

    constexpr StringLiteral OPTION_ALL = "all";
    constexpr StringLiteral OPTION_CONVERT_CONTROL = "convert-control";

    constexpr CommandSwitch FORMAT_SWITCHES[] = {
        {OPTION_ALL, []() { return msg::format(msgCmdFormatManifestOptAll); }},
        {OPTION_CONVERT_CONTROL, []() { return msg::format(msgCmdFormatManifestOptConvertControl); }},
    };

} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandFormatManifestMetadata = {
        [] { return create_example_string("format-manifest --all"); },
        0,
        SIZE_MAX,
        {FORMAT_SWITCHES, {}, {}},
        nullptr,
    };

    void command_format_manifest_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(CommandFormatManifestMetadata);

        auto& fs = paths.get_filesystem();
        bool has_error = false;

        const bool format_all = Util::Sets::contains(parsed_args.switches, OPTION_ALL);
        const bool convert_control = Util::Sets::contains(parsed_args.switches, OPTION_CONVERT_CONTROL);

        if (!format_all && convert_control)
        {
            msg::println_warning(msgMissingArgFormatManifest);
        }

        if (!format_all && parsed_args.command_arguments.empty())
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgFailedToFormatMissingFile);
        }

        std::vector<ToWrite> to_write;

        const auto add_file = [&to_write, &has_error](Optional<ToWrite>&& opt) {
            if (auto t = opt.get())
            {
                to_write.push_back(std::move(*t));
            }
            else
            {
                has_error = true;
            }
        };

        for (Path path : parsed_args.command_arguments)
        {
            if (path.is_relative())
            {
                path = paths.original_cwd / path;
            }

            if (path.filename() == "CONTROL")
            {
                add_file(read_control_file(fs, std::move(path)));
            }
            else
            {
                add_file(read_manifest(fs, std::move(path)));
            }
        }

        if (format_all)
        {
            for (const auto& dir : fs.get_directories_non_recursive(paths.builtin_ports_directory(), VCPKG_LINE_INFO))
            {
                auto control_path = dir / "CONTROL";
                auto manifest_path = dir / "vcpkg.json";
                auto manifest_exists = fs.exists(manifest_path, IgnoreErrors{});
                auto control_exists = fs.exists(control_path, IgnoreErrors{});

                if (manifest_exists && control_exists)
                {
                    Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgControlAndManifestFilesPresent, msg::path = dir);
                }

                if (manifest_exists)
                {
                    add_file(read_manifest(fs, std::move(manifest_path)));
                }
                if (convert_control && control_exists)
                {
                    add_file(read_control_file(fs, std::move(control_path)));
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
