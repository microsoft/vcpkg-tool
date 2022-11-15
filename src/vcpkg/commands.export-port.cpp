#include <vcpkg/base/git.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h> // for extract_tar_cmake
#include <vcpkg/commands.export-port.h>
#include <vcpkg/configuration.h>
#include <vcpkg/metrics.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;

    void copy_port_files(Filesystem& fs, StringView port_name, const Path& source, const Path& destination)
    {
        std::error_code ec;
        if (!fs.exists(source, VCPKG_LINE_INFO))
        {
            msg::println_error(msgExportPortFilesMissing, msg::package_name = port_name, msg::path = source);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        auto port_files = fs.get_regular_files_recursive(source, ec);
        if (ec)
        {
            msg::println_error(msgExportPortFilesMissing, msg::package_name = port_name, msg::path = source);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        for (const auto& file : Util::fmap(port_files, [&source](StringView&& str) -> Path {
                 return str.substr(source.generic_u8string().size() + 1);
             }))
        {
            const auto src_path = source / file;
            const auto dst_path = destination / file;
            fs.create_directories(dst_path.parent_path(), IgnoreErrors{});
            fs.copy_file(src_path, dst_path, CopyOptions::overwrite_existing, VCPKG_LINE_INFO);
        }
    }

    void export_registry_port_version(const VcpkgPaths& paths,
                                      StringView port_name,
                                      const Version& version,
                                      const Path& destination)
    {
        // Registry configuration
        const auto& config = paths.get_configuration();
        auto registries = config.instantiate_registry_set(paths);
        auto source = registries->fetch_port_files(port_name, version);
        if (!source)
        {
            msg::println_error(source.error());
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        copy_port_files(paths.get_filesystem(), port_name, source.value_or_exit(VCPKG_LINE_INFO).path, destination);
        msg::println(msgExportPortSuccess, msg::path = destination);
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void export_classic_mode_port_version(const VcpkgPaths& paths,
                                          StringView port_name,
                                          const Version& version,
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
            if (entry.version == version)
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
            msg::println(LocalizedString().append_indent().append_raw(entry.version.to_string()));
        }
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    void export_classic_mode_port(const VcpkgPaths& paths, StringView port_name, const Path& destination)
    {
        auto& fs = paths.get_filesystem();
        const auto port_dir = paths.builtin_ports_directory() / port_name;
        copy_port_files(fs, port_name, port_dir, destination);
        msg::println(msgExportPortSuccess, msg::path = destination);
        msg::println(msgExportPortSuccess, msg::path = destination);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}

namespace vcpkg::Commands::ExportPort
{
    constexpr static StringLiteral OPTION_ADD_VERSION_SUFFIX = "add-version-suffix";
    constexpr static StringLiteral OPTION_FORCE = "force";
    constexpr static StringLiteral OPTION_VERSION = "version";
    constexpr static StringLiteral OPTION_NO_REGISTRIES = "no-registries";
    constexpr static StringLiteral OPTION_NO_SUBDIR = "no-subdir";

    constexpr static CommandSwitch SWITCHES[]{
        {OPTION_ADD_VERSION_SUFFIX, "adds the port version as a suffix to the output subdirectory"},
        {OPTION_FORCE, "overwrite existing files in destination"},
        {OPTION_NO_REGISTRIES, "ignore configured registries when resolving port"},
        {OPTION_NO_SUBDIR, "don't create a subdirectory for the port"},
    };

    constexpr static CommandSetting SETTINGS[]{
        {OPTION_VERSION, "export port files from a specific version"},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("x-export-port fmt 8.11.0#2git ../my-overlay-ports"),
        1,
        2,
        {{SWITCHES}, {SETTINGS}, {/*multisettings*/}},
        nullptr,
    };

    void ExportPortCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        auto options = args.parse_arguments(COMMAND_STRUCTURE);
        bool add_suffix = Util::Sets::contains(options.switches, OPTION_ADD_VERSION_SUFFIX);
        bool force = Util::Sets::contains(options.switches, OPTION_FORCE);
        bool no_subdir = Util::Sets::contains(options.switches, OPTION_NO_SUBDIR);
        bool include_registries = !Util::Sets::contains(options.switches, OPTION_NO_REGISTRIES);
        const auto& config = paths.get_configuration().config;
        const bool has_registries = Util::any_of(config.registries, [](const auto& reg) { return reg.kind != "artifact"; });

        Optional<Version> maybe_version;
        auto it = options.settings.find(OPTION_VERSION);
        if (it != options.settings.end())
        {
            const auto& version_arg = it->second;
            auto version_segments = Strings::split(version_arg, '#');
            if (version_segments.size() > 2)
            {
                msg::println_error(msgExportPortVersionArgumentInvalid, msg::version = version_arg);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            int port_version = 0;
            if (version_segments.size() == 2)
            {
                auto maybe_port_version = Strings::strto<int>(version_segments[1]);
                if (!maybe_port_version)
                {
                    msg::println_error(msgExportPortVersionArgumentInvalid, msg::version = version_arg);
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }
                port_version = maybe_port_version.value_or_exit(VCPKG_LINE_INFO);
            }

            maybe_version.emplace(version_segments[0], port_version);
        }

        if (add_suffix)
        {
            if (!maybe_version)
            {
                add_suffix = false;
                msg::println_warning(msgExportPortIgnoreSuffixNoVersion);
            }

            if (no_subdir)
            {
                add_suffix = false;
                msg::println_warning(msgExportPortIgnoreSuffixNoSubdir);
            }
        }

        const auto& port_name = args.command_arguments[0];
        Optional<Path> maybe_destination;
        if (args.command_arguments.size() == 2)
        {
            maybe_destination.emplace(args.command_arguments[1]);
        }

        if (!maybe_destination)
        {
            if (paths.overlay_ports.empty())
            {
                msg::println_error(msgExportPortNoDestination);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            maybe_destination.emplace(paths.overlay_ports.front());
        }

        auto& fs = paths.get_filesystem();
        // Made lexically normal for prettier printing
        Path final_path =
            fs.absolute(maybe_destination.value_or_exit(VCPKG_LINE_INFO), VCPKG_LINE_INFO).lexically_normal();
        if (!no_subdir)
        {
            auto subdir = port_name;
            if (add_suffix)
            {
                subdir += fmt::format(
                    "-{}", Strings::replace_all(maybe_version.value_or_exit(VCPKG_LINE_INFO).to_string(), "#", "-"));
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

        get_global_metrics_collector().track_string(StringMetric::ExportedPort, Hash::get_string_sha256(port_name));
        if (auto version = maybe_version.get())
        {
            get_global_metrics_collector().track_string(StringMetric::ExportedVersion,
                                                        Hash::get_string_sha256(version->to_string()));
            if (include_registries && has_registries)
            {
                export_registry_port_version(paths, port_name, *version, final_path);
            }
            else
            {
                export_classic_mode_port_version(paths, port_name, *version, final_path);
            }
        }
        else
        {
            if (include_registries)
            {

            }

            export_classic_mode_port(paths, port_name, final_path);
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
