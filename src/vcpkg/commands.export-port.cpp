#include <vcpkg/base/git.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h>
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

    void export_registry_port(const VcpkgPaths& paths,
                              StringView port_name,
                              Optional<Version> version,
                              const Path& destination)
    {
        // Registry configuration
        const auto& config = paths.get_configuration();
        auto registries = config.instantiate_registry_set(paths);
        auto source = version ? registries->fetch_port_files(port_name, version.value_or_exit(VCPKG_LINE_INFO))
                              : registries->fetch_port_files(port_name);
        if (!source)
        {
            msg::println_error(source.error());
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        copy_port_files(paths.get_filesystem(), port_name, source.value_or_exit(VCPKG_LINE_INFO).path, destination);
        msg::println(msgExportPortSuccess, msg::path = destination);
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void export_classic_mode_versioned(const VcpkgPaths& paths,
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

    void export_classic_mode_unversioned(const VcpkgPaths& paths, StringView port_name, const Path& destination)
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
    constexpr static StringLiteral OPTION_FORCE = "force";
    constexpr static StringLiteral OPTION_NO_REGISTRIES = "no-registries";
    constexpr static StringLiteral OPTION_SUBDIR = "subdir";

    constexpr static CommandSwitch SWITCHES[]{
        {OPTION_FORCE, []() { return msg::format(msgCmdExportPortForce); }},
        {OPTION_NO_REGISTRIES, []() { return msg::format(msgCmdExportPortNoRegistries); }},
        {OPTION_SUBDIR, []() { return msg::format(msgCmdExportPortSubdir); }},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("x-export-port fmt@8.11.0#2 ../my-overlay-ports"),
        2,
        2,
        {{SWITCHES}, {/*settings*/}, {/*multisettings*/}},
        nullptr,
    };

    void ExportPortCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        auto options = args.parse_arguments(COMMAND_STRUCTURE);
        bool force = Util::Sets::contains(options.switches, OPTION_FORCE);
        bool subdir = Util::Sets::contains(options.switches, OPTION_SUBDIR);
        bool include_registries = !Util::Sets::contains(options.switches, OPTION_NO_REGISTRIES);
        const auto& config = paths.get_configuration().config;
        const bool has_registries =
            Util::any_of(config.registries, [](const auto& reg) { return reg.kind != "artifact"; });

        const auto package_spec_arg = args.command_arguments[0];
        const auto maybe_package_spec = parse_qualified_specifier(package_spec_arg);
        if (!maybe_package_spec.has_value())
        {
            // print error message
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        const auto package_spec = maybe_package_spec.value_or_exit(VCPKG_LINE_INFO);
        const auto port_name = package_spec.name;
        const auto& maybe_version = package_spec.version;

        auto destination = Path(args.command_arguments[1]);
        if (subdir)
        {
            destination /= port_name;
        }
        auto& fs = paths.get_filesystem();
        const Path final_path = fs.absolute(destination, VCPKG_LINE_INFO).lexically_normal();

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
                export_registry_port(paths, port_name, *version, final_path);
            }
            else
            {
                export_classic_mode_versioned(paths, port_name, *version, final_path);
            }
        }
        else
        {
            if (include_registries)
            {
                // fetchs baseline version on the configured registry
                export_registry_port(paths, port_name, nullopt, final_path);
            }

            export_classic_mode_unversioned(paths, port_name, final_path);
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
