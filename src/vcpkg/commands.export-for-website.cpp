#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>

#include <vcpkg/commands.export-for-website.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;

    enum class FileType
    {
        ControlFile,
        ManifestFile,
    };
    struct Port
    {
        SourceControlFile scf;
        FileType fileType;
    };

    Optional<SourceControlFile> read_manifest(Files::Filesystem& fs, fs::path&& manifest_path)
    {
        auto path_string = fs::u8string(manifest_path);
        Debug::print("Reading ", path_string, "\n");
        auto contents = fs.read_contents(manifest_path, VCPKG_LINE_INFO);
        auto parsed_json_opt = Json::parse(contents, manifest_path);
        if (!parsed_json_opt.has_value())
        {
            System::printf(
                System::Color::error, "Failed to parse %s: %s\n", path_string, parsed_json_opt.error()->format());
            return nullopt;
        }

        const auto& parsed_json = parsed_json_opt.value_or_exit(VCPKG_LINE_INFO).first;
        if (!parsed_json.is_object())
        {
            System::printf(System::Color::error, "The file %s is not an object\n", path_string);
            return nullopt;
        }

        auto parsed_json_obj = parsed_json.object();

        auto scf = SourceControlFile::parse_manifest_file(manifest_path, parsed_json_obj);
        if (!scf.has_value())
        {
            System::printf(System::Color::error, "Failed to parse manifest file: %s\n", path_string);
            print_error_message(scf.error());
            return nullopt;
        }

        return std::move(*scf.value_or_exit(VCPKG_LINE_INFO));
    }

    Optional<SourceControlFile> read_control_file(Files::Filesystem& fs, fs::path&& control_path)
    {
        std::error_code ec;
        auto control_path_string = fs::u8string(control_path);
        Debug::print("Reading ", control_path_string, "\n");

        auto manifest_path = control_path.parent_path();
        manifest_path /= fs::u8path("vcpkg.json");

        auto contents = fs.read_contents(control_path, VCPKG_LINE_INFO);
        auto paragraphs = Paragraphs::parse_paragraphs(contents, control_path_string);

        if (!paragraphs)
        {
            System::printf(System::Color::error,
                           "Failed to read paragraphs from %s: %s\n",
                           control_path_string,
                           paragraphs.error());
            return {};
        }
        auto scf_res = SourceControlFile::parse_control_file(fs::u8string(control_path),
                                                             std::move(paragraphs).value_or_exit(VCPKG_LINE_INFO));
        if (!scf_res)
        {
            System::printf(System::Color::error, "Failed to parse control file: %s\n", control_path_string);
            print_error_message(scf_res.error());
            return {};
        }

        return std::move(*scf_res.value_or_exit(VCPKG_LINE_INFO));
    }

    void write_file(Files::Filesystem& fs,
                    const fs::path& outputFile,
                    const std::vector<Port>& datas,
                    bool include_empty_fields)
    {
        Json::Object root;
        Json::Array ports;
        for (auto& data : datas)
        {
            auto port = serialize_manifest_for_export(data.scf, include_empty_fields);
            port.insert("isManifestFile", Json::Value::boolean(data.fileType == FileType::ManifestFile));
            ports.push_back(std::move(port));
        }
        root.insert("ports", ports);

        std::error_code ec;
        fs.write_contents(outputFile, Json::stringify(root, {}), ec);
        if (ec)
        {
            Checks::exit_with_message(
                VCPKG_LINE_INFO, "Failed to write manifest file %s: %s\n", "file_to_write_string", ec.message());
        }
    }
}

namespace vcpkg::Commands::ExportForWebsite
{
    static constexpr StringLiteral OPTION_INCLUDE_EMPTY_FIELDS = "include-empty-fields";

    const CommandSwitch FORMAT_SWITCHES[] = {
        {OPTION_INCLUDE_EMPTY_FIELDS, "Includes empty fields, otherwise they are omitted."},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string(R"###(format-manifest --include-empty-fields)###"),
        1,
        1,
        {FORMAT_SWITCHES, {}, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

        auto& fs = paths.get_filesystem();
        bool has_error = false;

        const bool include_empty_fields = Util::Sets::contains(parsed_args.switches, OPTION_INCLUDE_EMPTY_FIELDS);

        if (args.command_arguments.size() != 1)
        {
            Checks::exit_with_message(VCPKG_LINE_INFO,
                                      "Please provide a filename as argument to which the output will be written.");
        }
        fs::path outputPath = args.command_arguments.at(0);

        std::vector<Port> to_write;

        const auto add_file = [&to_write, &has_error](Optional<SourceControlFile>&& opt, FileType fileType) {
            if (auto t = opt.get())
                to_write.push_back({std::move(*t), fileType});
            else
                has_error = true;
        };

        for (const auto& dir : fs::directory_iterator(paths.builtin_ports_directory()))
        {
            auto control_path = dir.path() / fs::u8path("CONTROL");
            auto manifest_path = dir.path() / fs::u8path("vcpkg.json");
            auto manifest_exists = fs.exists(manifest_path);
            auto control_exists = fs.exists(control_path);

            Checks::check_exit(VCPKG_LINE_INFO,
                               !manifest_exists || !control_exists,
                               "Both a manifest file and a CONTROL file exist in port directory: %s",
                               fs::u8string(dir.path()));

            if (manifest_exists)
            {
                add_file(read_manifest(fs, std::move(manifest_path)), FileType::ManifestFile);
            }
            if (control_exists)
            {
                add_file(read_control_file(fs, std::move(control_path)), FileType::ControlFile);
            }
        }

        write_file(fs, outputPath, to_write, include_empty_fields);

        if (has_error)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        else
        {
            System::printf("Output has been written to %s\n", fs::u8string(fs.absolute(VCPKG_LINE_INFO, outputPath)));
            Checks::exit_success(VCPKG_LINE_INFO);
        }
    }

    void ExportForWebsiteCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        ExportForWebsite::perform_and_exit(args, paths);
    }
}
