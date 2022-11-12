#include <vcpkg/base/git.h>
#include <vcpkg/base/json.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.export-port.h>
#include <vcpkg/registries.h> // for versions db deserializer
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;

    ExpectedL<std::vector<VersionDbEntry>> parse_versions_file(StringView contents, StringView origin)
    {
        auto maybe_db = std::move(Json::parse(contents, origin));
        if (!maybe_db)
        {
            return msg::format_error(msgAddVersionUnableToParseVersionsFile, msg::path = origin)
                .append_raw("\n")
                .append_indent()
                .append_raw(maybe_db.error()->to_string());
        }

        auto db = std::move(maybe_db.value_or_exit(VCPKG_LINE_INFO));
        if (!db.first.is_object())
        {
            return msg::format_error(msgAddVersionUnableToParseVersionsFile, msg::path = origin)
                .append_raw("\n")
                .append_indent()
                .append(msgJsonValueNotObject);
        }

        Json::Reader r;
        auto maybe_versions = r.visit(std::move(db.first.object(VCPKG_LINE_INFO)), GitVersionDbDeserializer::instance);
        if (!r.errors().empty())
        {
            auto error_msg =
                msg::format_error(msgAddVersionUnableToParseVersionsFile, msg::path = origin).append_raw("\n");
            for (auto&& error : r.errors())
            {
                error_msg.append_indent().append_raw(error).append_raw("\n");
            }
            return error_msg;
        }

        return std::move(maybe_versions.value_or_exit(VCPKG_LINE_INFO));
    }

    void export_classic_mode_port_version(
        const VcpkgPaths& paths, StringView port_name, StringView version, int port_version, const Path& destination)
    {
        const auto db_file =
            paths.builtin_registry_versions / fmt::format("{}-", port_name[0]) / fmt::format("{}.json", port_name);

        auto& fs = paths.get_filesystem();
        if (!fs.exists(db_file, VCPKG_LINE_INFO))
        {
            msg::println_error(msgAddVersionFileNotFound, msg::path = db_file);
            // Emit suggestion to check that port name is correct.
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        auto contents = fs.read_contents(db_file, IgnoreErrors{});
        auto maybe_db = parse_versions_file(contents, db_file);
        if (!maybe_db)
        {
            msg::println_error(maybe_db.error());
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        const auto requested_version = Version{
            std::move(version.to_string()),
            port_version,
        };
        auto db = std::move(maybe_db.value_or_exit(VCPKG_LINE_INFO));
        for (auto&& entry : db)
        {
            if (entry.version == requested_version)
            {
                const auto archive_path =
                    destination /
                    fmt::format("{}-{}-{}.tar", port_name, requested_version.text(), requested_version.port_version());
                const auto final_path = destination / port_name;
                fs.create_directories(final_path, VCPKG_LINE_INFO);

                auto maybe_export = git_export_archive(paths.git_builtin_config(), entry.git_tree, archive_path);
                if (!maybe_export)
                {
                    msg::println_error(maybe_export.error());
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                extract_tar_cmake(Tools::CMAKE, archive_path, final_path);
                fs.remove(archive_path, VCPKG_LINE_INFO);
                // TODO: print success message
                msg::write_unlocalized_text_to_stdout(Color::none,
                                                      fmt::format("Port files have been exported to {}\n", final_path));
                Checks::exit_success(VCPKG_LINE_INFO);
            }
        }

        // TODO: Print version not found and list of available versions
        msg::write_unlocalized_text_to_stdout(Color::none,
                                              fmt::format("Version {} not found.\n", requested_version.to_string()));
        for (auto&& entry : db)
        {
            msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("{}\n", entry.version.to_string()));
        }
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    void export_classic_mode_port(const VcpkgPaths& paths, StringView port_name, const Path& destination)
    {
        auto& fs = paths.get_filesystem();

        const auto port_dir = paths.builtin_ports_directory() / port_name;

        std::error_code ec;
        auto port_files = fs.get_regular_files_recursive(port_dir, ec);
        if (ec)
        {
            msg::write_unlocalized_text_to_stderr(Color::error,
                                                  fmt::format("Failed to get files for port {}\n", port_name));
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        const auto final_path = destination / port_name;
        for (const auto& file : Util::fmap(port_files, [&port_dir](StringView&& str) -> Path {
                 return str.substr(port_dir.generic_u8string().size() + 1);
             }))
        {
            const auto src_path = port_dir / file;
            const auto dst_path = final_path / file;
            fs.create_directories(dst_path.parent_path(), IgnoreErrors{});
            fs.copy_file(src_path, dst_path, CopyOptions::overwrite_existing, VCPKG_LINE_INFO);
        }
        msg::write_unlocalized_text_to_stdout(Color::none,
                                              fmt::format("Port files have been exported to {}\n", final_path));
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}

namespace vcpkg::Commands::ExportPort
{
    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("x-export-port fmt 8.11.0#0 ../my-overlay-ports"),
        2,
        3,
        {{}, {}, {}},
        nullptr,
    };

    void ExportPortCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        // validate command arguments
        args.parse_arguments(COMMAND_STRUCTURE);

        const auto& port_name = args.command_arguments[0];
        const auto& output_path = args.command_arguments.back();
        Optional<std::string> maybe_raw_version;
        if (args.command_arguments.size() == 3)
        {
            maybe_raw_version = args.command_arguments[1];
        }

        auto& fs = paths.get_filesystem();
        const auto final_path = Path(output_path) / port_name;
        if (fs.exists(final_path, IgnoreErrors{}) && !fs.is_empty(final_path, IgnoreErrors{}))
        {
            msg::write_unlocalized_text_to_stdout(
                Color::error,
                fmt::format("Export path {} already exists and is not empty.\n", final_path.lexically_normal()));
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        if (paths.manifest_mode_enabled())
        {
            msg::write_unlocalized_text_to_stdout(Color::error, "This command doesn't work on manifest mode.\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        // classic mode
        if (auto raw_version = maybe_raw_version.get())
        {
            // user requested a specific version
            auto version_segments = Strings::split(*raw_version, '#');
            if (version_segments.size() > 2)
            {
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
            const auto& version = version_segments[0];
            auto port_version = version_segments.size() == 1 ? 0 : Strings::strto<int>(version_segments[1]).value_or(0);
            export_classic_mode_port_version(paths, port_name, version, port_version, output_path);
        }
        else
        {
            // just copy local files
            export_classic_mode_port(paths, port_name, output_path);
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}