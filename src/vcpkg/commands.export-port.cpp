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

    void export_classic_mode_port_version(
        const VcpkgPaths& paths, StringView port_name, StringView version, int port_version, const Path& destination)
    {
        const auto db_file = paths.builtin_registry_versions / fmt::format("{}-/{}.json", port_name[0], port_name);

        auto& fs = paths.get_filesystem();
        if (!fs.exists(db_file, VCPKG_LINE_INFO))
        {
            msg::println_error(msgAddVersionFileNotFound, msg::path = db_file);
            // Emit suggestion to check that port name is correct.
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        auto contents = fs.read_contents(db_file, IgnoreErrors{});
        auto maybe_db = parse_git_versions_file(contents, db_file);
        if (!maybe_db)
        {
            msg::println_error(maybe_db.error());
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        auto db = std::move(maybe_db.value_or_exit(VCPKG_LINE_INFO));
        for (auto&& entry : db)
        {
            if (entry.version.text() == version && entry.version.port_version() == port_version)
            {
                const Path parent_dir = destination.parent_path();
                fs.create_directories(parent_dir, VCPKG_LINE_INFO);

                const auto archive_path =
                    parent_dir /
                    fmt::format("{}-{}-{}.tar", port_name, entry.version.text(), entry.version.port_version());

                auto maybe_export = git_export_archive(paths.git_builtin_config(), entry.git_tree, archive_path);
                if (!maybe_export)
                {
                    msg::println_error(maybe_export.error());
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                fs.create_directories(destination, VCPKG_LINE_INFO);
                extract_tar_cmake(Tools::CMAKE, archive_path, destination);
                fs.remove(archive_path, VCPKG_LINE_INFO);
                // TODO: print success message
                msg::write_unlocalized_text_to_stdout(
                    Color::none, fmt::format("Port files have been exported to {}\n", destination));
                Checks::exit_success(VCPKG_LINE_INFO);
            }
        }

        // TODO: Print version not found and list of available versions
        msg::write_unlocalized_text_to_stdout(
            Color::none, fmt::format("Version {} not found.\n", Version(version.to_string(), port_version)));
        for (auto&& entry : db)
        {
            msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("{}\n", entry.version));
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

        for (const auto& file : Util::fmap(port_files, [&port_dir](StringView&& str) -> Path {
                 return str.substr(port_dir.generic_u8string().size() + 1);
             }))
        {
            const auto src_path = port_dir / file;
            const auto dst_path = destination / file;
            fs.create_directories(dst_path.parent_path(), IgnoreErrors{});
            fs.copy_file(src_path, dst_path, CopyOptions::overwrite_existing, VCPKG_LINE_INFO);
        }
        msg::write_unlocalized_text_to_stdout(Color::none,
                                              fmt::format("Port files have been exported to {}\n", destination));
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}

namespace vcpkg::Commands::ExportPort
{
    constexpr static StringLiteral OPTION_ADD_VERSION_SUFFIX = "add-version-suffix";
    constexpr static StringLiteral OPTION_FORCE = "force";
    constexpr static StringLiteral OPTION_NO_SUBDIR = "no-subdir";

    constexpr static CommandSwitch SWITCHES[]{
        {OPTION_ADD_VERSION_SUFFIX, "adds the port version as a suffix to the output subdirectory"},
        {OPTION_FORCE, "overwrite existing files in destination"},
        {OPTION_NO_SUBDIR, "don't create a subdirectory for the port"},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("x-export-port fmt 8.11.0#0 ../my-overlay-ports"),
        2,
        3,
        {{SWITCHES}, {/*settings*/}, {/*multisettings*/}},
        nullptr,
    };

    void ExportPortCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        auto options = args.parse_arguments(COMMAND_STRUCTURE);

        bool add_suffix = Util::Sets::contains(options.switches, OPTION_ADD_VERSION_SUFFIX);
        bool force = Util::Sets::contains(options.switches, OPTION_FORCE);
        bool no_subdir = Util::Sets::contains(options.switches, OPTION_NO_SUBDIR);

        if (add_suffix && no_subdir)
        {
            add_suffix = false;
            msg::println_warning(LocalizedString::from_raw(
                "Ignoring option --add--version-suffix because option --no-subdir was passed."));
        }

        const auto& port_name = args.command_arguments[0];
        const auto& output_path = args.command_arguments.back();
        Optional<std::string> maybe_raw_version;
        if (args.command_arguments.size() == 3)
        {
            maybe_raw_version = args.command_arguments[1];
        }

        auto& fs = paths.get_filesystem();
        // Made lexically normal for prettier printing
        Path final_path = fs.absolute(output_path, VCPKG_LINE_INFO).lexically_normal();
        if (!no_subdir)
        {
            auto subdir = port_name;
            if (add_suffix)
            {
                if (maybe_raw_version)
                {
                    subdir += fmt::format(
                        "-{}", Strings::replace_all(maybe_raw_version.value_or_exit(VCPKG_LINE_INFO), "#", "-"));
                }
                else
                {
                    msg::println_warning(LocalizedString::from_raw(
                        "Ignoring option --add-version-suffix because no version argument was passed."));
                }
            }
            final_path /= subdir;
        }

        if (force)
        {
            std::error_code ec;
            fs.remove_all(final_path, ec);
            if (ec)
            {
                msg::println_error(LocalizedString::from_raw(fmt::format("Could not delete directory {}", final_path)));
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }
        else if (fs.exists(final_path, IgnoreErrors{}) && !fs.is_empty(final_path, IgnoreErrors{}))
        {
            msg::write_unlocalized_text_to_stdout(
                Color::error, fmt::format("Export path {} already exists and is not empty.\n", final_path));
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
            export_classic_mode_port_version(paths, port_name, version, port_version, final_path);
        }
        else
        {
            // just copy local files
            export_classic_mode_port(paths, port_name, final_path);
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}