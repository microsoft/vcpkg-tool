#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.format-manifest.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;

    struct ToWrite
    {
        SourceControlFile scf;
        Path file_to_write;
        Path original_path;
        std::string original_source;
    };

    Optional<ToWrite> read_manifest(Filesystem& fs, Path&& manifest_path)
    {
        auto path_string = manifest_path.native();
        Debug::println("Reading ", path_string);
        auto contents = fs.read_contents(manifest_path, VCPKG_LINE_INFO);
        auto parsed_json_opt = Json::parse(contents, manifest_path);
        if (!parsed_json_opt)
        {
            msg::println_error(msg::format(msgFailedToParseJson, msg::path = path_string)
                                   .append_raw(": ")
                                   .append_raw(parsed_json_opt.error()->to_string()));
            return nullopt;
        }

        const auto& parsed_json = parsed_json_opt.value_or_exit(VCPKG_LINE_INFO).first;
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
            return nullopt;
        }

        return ToWrite{
            std::move(*scf.value_or_exit(VCPKG_LINE_INFO)),
            manifest_path,
            manifest_path,
            std::move(contents),
        };
    }

    Optional<ToWrite> read_control_file(Filesystem& fs, Path&& control_path)
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
            SourceControlFile::parse_control_file(control_path, std::move(paragraphs).value_or_exit(VCPKG_LINE_INFO));
        if (!scf_res)
        {
            msg::println_error(msgFailedToParseControl, msg::path = control_path);
            print_error_message(scf_res.error());
            return {};
        }

        return ToWrite{
            std::move(*scf_res.value_or_exit(VCPKG_LINE_INFO)),
            manifest_path,
            control_path,
            std::move(contents),
        };
    }

    void open_for_write(Filesystem& fs, const ToWrite& data)
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

        auto check = SourceControlFile::parse_project_manifest_object(StringView{}, res, null_sink);
        if (!check)
        {
            msg::println_error(msgFailedToParseSerializedManifest, msg::path = data.scf.core_paragraph->name);
            print_error_message(check.error());
            Checks::exit_maybe_upgrade(VCPKG_LINE_INFO,
                                       R"(
=== Serialized manifest file ===
%s
)",
                                       Json::stringify(res));
        }

        auto check_scf = std::move(check).value_or_exit(VCPKG_LINE_INFO);
        if (*check_scf != data.scf)
        {
            Checks::msg_exit_maybe_upgrade(
                VCPKG_LINE_INFO,
                msg::format(msgMismatchedSerializedManifestSCF)
                    .append_raw(fmt::format("\n=== Original File ===\n{}\n=== Serialized File ===\n{}\n=== Original "
                                            "SFC ===\n{}\n=== Serialized SFC ===\n{}\n",
                                            data.original_source,
                                            Json::stringify(res, {}),
                                            Json::stringify(serialize_debug_manifest(data.scf)),
                                            Json::stringify(serialize_debug_manifest(*check_scf)))));
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
}

namespace vcpkg::Commands::FormatManifest
{
    static constexpr StringLiteral OPTION_ALL = "all";
    static constexpr StringLiteral OPTION_CONVERT_CONTROL = "convert-control";

    const CommandSwitch FORMAT_SWITCHES[] = {
        {OPTION_ALL, "Format all ports' manifest files."},
        {OPTION_CONVERT_CONTROL, "Convert CONTROL files to manifest files."},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string(R"###(format-manifest --all)###"),
        0,
        SIZE_MAX,
        {FORMAT_SWITCHES, {}, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

        auto& fs = paths.get_filesystem();
        bool has_error = false;

        const bool format_all = Util::Sets::contains(parsed_args.switches, OPTION_ALL);
        const bool convert_control = Util::Sets::contains(parsed_args.switches, OPTION_CONVERT_CONTROL);

        if (!format_all && convert_control)
        {
            msg::println_warning(msgMissingArgFormatManifest);
        }

        if (!format_all && args.command_arguments.empty())
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgFailedToFormatMissingFile);
        }

        std::vector<ToWrite> to_write;

        const auto add_file = [&to_write, &has_error](Optional<ToWrite>&& opt) {
            if (auto t = opt.get())
                to_write.push_back(std::move(*t));
            else
                has_error = true;
        };

        for (Path path : args.command_arguments)
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

    void FormatManifestCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        FormatManifest::perform_and_exit(args, paths);
    }
}
