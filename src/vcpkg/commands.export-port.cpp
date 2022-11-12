#include <vcpkg/base/git.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>

#include <vcpkg/archives.h> // for extract_tar_cmake
#include <vcpkg/commands.export-port.h>
#include <vcpkg/registries.h> // for versions db parsing
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;

    void export_classic_mode_port_version(const VcpkgPaths& paths,
                                          StringView port_name,
                                          StringView version,
                                          const Path& destination)
    {
        const auto db_file = paths.builtin_registry_versions / fmt::format("{}-/{}.json", port_name[0], port_name);

        auto& fs = paths.get_filesystem();
        if (!fs.exists(db_file, VCPKG_LINE_INFO))
        {
            msg::println_error(msgExportPortVersionsDbFileMissing, msg::package_name = port_name, msg::path = db_file);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        auto contents = fs.read_contents(db_file, VCPKG_LINE_INFO);
        auto maybe_db = parse_git_versions_file(contents, db_file);
        if (!maybe_db)
        {
            msg::println_error(maybe_db.error());
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        auto db = std::move(maybe_db.value_or_exit(VCPKG_LINE_INFO));
        for (auto&& entry : db)
        {
            if (entry.version.to_string() == version)
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
                msg::println(msgExportPortSuccess, msg::path = destination);
                Checks::exit_success(VCPKG_LINE_INFO);
            }
        }

        msg::println(msgExportPortVersionNotFound, msg::version = version);
        for (auto&& entry : db)
        {
            msg::println(LocalizedString().append_indent().append_raw(entry.version.to_string()).append_raw("\n"));
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
            msg::println_error(msgExportPortFilesMissing, msg::package_name = port_name, msg::path = port_dir);
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
        msg::println(msgExportPortSuccess, msg::path = destination);
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
        create_example_string("x-export-port fmt 8.11.0#2git ../my-overlay-ports"),
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
            // ignore --add-versio-suffix because --no-subdir was passed
            add_suffix = false;
            msg::println_warning(msgExportPortIgnoreSuffixNoSubdir);
        }

        const auto& port_name = args.command_arguments[0];
        const auto& output_path = args.command_arguments.back();
        Optional<std::string> maybe_version;
        if (args.command_arguments.size() == 3)
        {
            maybe_version = args.command_arguments[1];
        }

        auto& fs = paths.get_filesystem();
        // Made lexically normal for prettier printing
        Path final_path = fs.absolute(output_path, VCPKG_LINE_INFO).lexically_normal();
        if (!no_subdir)
        {
            auto subdir = port_name;
            if (add_suffix)
            {
                if (maybe_version)
                {
                    subdir += fmt::format("-{}",
                                          Strings::replace_all(maybe_version.value_or_exit(VCPKG_LINE_INFO), "#", "-"));
                }
                else
                {
                    // ignore --add-version-suffix because there's no version
                    msg::println_warning(msgExportPortIgnoreSuffixNoVersion);
                }
            }
            final_path /= subdir;
        }

        if (force)
        {
            fs.remove_all(final_path, VCPKG_LINE_INFO);
        }
        else if (fs.exists(final_path, IgnoreErrors{}) && !fs.is_empty(final_path, IgnoreErrors{}))
        {
            msg::println_error(msgExportPortPathExistsAndNotEmpty, msg::path = final_path);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        if (paths.manifest_mode_enabled())
        {
            // TODO: spec out how this command works in manifest mode and
            // when using registries.
            Debug::println("This command doesn't work on manifest mode");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        // classic mode
        if (auto version = maybe_version.get())
        {
            export_classic_mode_port_version(paths, port_name, *version, final_path);
        }
        else
        {
            // just copy local files
            export_classic_mode_port(paths, port_name, final_path);
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
