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

    void export_classic_mode_port_to_path(
        const VcpkgPaths& paths, StringView port_name, StringView version, int port_version, Path output_path)
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
                    output_path /
                    fmt::format("{}-{}-{}.tar", port_name, requested_version.text(), requested_version.port_version());
                const auto final_path = output_path / port_name;
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
                                                      fmt::format("Port files have been exported to {}", final_path));
                Checks::exit_success(VCPKG_LINE_INFO);
            }
        }

        // TODO: Print version not found and list of available versions
        msg::write_unlocalized_text_to_stdout(Color::none,
                                              fmt::format("Version {} not found", requested_version.to_string()));
        for (auto&& entry : db)
        {
            msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("{}\n", entry.version.to_string()));
        }
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
}

namespace vcpkg::Commands::ExportPort
{
    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("x-export-port fmt 8.11.0#0 ../my-overlay-ports"),
        3,
        3,
        {{}, {}, {}},
        nullptr,
    };

    void ExportPortCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        // validate command arguments
        args.parse_arguments(COMMAND_STRUCTURE);

        const auto& port_name = args.command_arguments[0];
        const auto& raw_version = args.command_arguments[1];
        const auto& output_path = args.command_arguments[2];

        auto& fs = paths.get_filesystem();
        const auto final_path = Path(output_path) / port_name;
        if (fs.exists(final_path, IgnoreErrors{}) && !fs.is_empty(final_path, IgnoreErrors{}))
        {
            msg::write_unlocalized_text_to_stdout(
                Color::error,
                fmt::format("Export path {} already exists and is not empty.\n", final_path.lexically_normal()));
        }

        auto version_segments = Strings::split(raw_version, '#');
        if (version_segments.size() > 2)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        const auto& version = version_segments[0];
        auto port_version = version_segments.size() == 1 ? 0 : Strings::strto<int>(version_segments[1]).value_or(0);
        export_classic_mode_port_to_path(paths, port_name, version, port_version, output_path);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}