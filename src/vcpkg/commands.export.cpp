#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/commands.export.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/input.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    std::string create_nuspec_file_contents(const Path& raw_exported_dir,
                                            const Path& targets_redirect_path,
                                            const Path& props_redirect_path,
                                            const std::string& nuget_id,
                                            const std::string& nupkg_version,
                                            const std::string& nuget_description)
    {
        XmlSerializer xml;
        xml.open_tag("package").line_break();
        xml.open_tag("metadata").line_break();
        xml.simple_tag("id", nuget_id).line_break();
        xml.simple_tag("version", nupkg_version).line_break();
        xml.simple_tag("authors", "vcpkg").line_break();
        xml.simple_tag("description", nuget_description).line_break();
        xml.close_tag("metadata").line_break();
        xml.open_tag("files").line_break();
        xml.start_complex_open_tag("file")
            .text_attr("src", raw_exported_dir.native() + "\\installed\\**")
            .text_attr("target", "installed")
            .finish_self_closing_complex_tag()
            .line_break();

        xml.start_complex_open_tag("file")
            .text_attr("src", raw_exported_dir.native() + "\\scripts\\**")
            .text_attr("target", "scripts")
            .finish_self_closing_complex_tag()
            .line_break();

        xml.start_complex_open_tag("file")
            .text_attr("src", raw_exported_dir.native() + "\\.vcpkg-root")
            .text_attr("target", "")
            .finish_self_closing_complex_tag()
            .line_break();

        xml.start_complex_open_tag("file")
            .text_attr("src", targets_redirect_path)
            .text_attr("target", Strings::concat("build\\native\\", nuget_id, ".targets"))
            .finish_self_closing_complex_tag()
            .line_break();

        xml.start_complex_open_tag("file")
            .text_attr("src", props_redirect_path)
            .text_attr("target", Strings::concat("build\\native\\", nuget_id, ".props"))
            .finish_self_closing_complex_tag()
            .line_break();

        xml.close_tag("files").line_break();
        xml.close_tag("package").line_break();

        return std::move(xml.buf);
    }

    std::string create_targets_redirect(const std::string& target_path) noexcept
    {
        return fmt::format(R"###(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Condition="Exists('{}')" Project="{}" />
</Project>
)###",
                           target_path,
                           target_path);
    }

    void print_export_plan(const std::map<ExportPlanType, std::vector<const ExportPlanAction*>>& group_by_plan_type)
    {
        static constexpr ExportPlanType ORDER[] = {
            ExportPlanType::ALREADY_BUILT,
            ExportPlanType::NOT_BUILT,
        };

        for (const ExportPlanType plan_type : ORDER)
        {
            const auto it = group_by_plan_type.find(plan_type);
            if (it == group_by_plan_type.cend())
            {
                continue;
            }

            std::vector<const ExportPlanAction*> cont = it->second;
            std::sort(cont.begin(), cont.end(), &ExportPlanAction::compare_by_name);
            LocalizedString msg;
            if (plan_type == ExportPlanType::ALREADY_BUILT)
                msg = msg::format(msgExportingAlreadyBuiltPackages);
            else if (plan_type == ExportPlanType::NOT_BUILT)
                msg = msg::format(msgPackagesToInstall);
            else
                Checks::unreachable(VCPKG_LINE_INFO);

            msg.append_raw('\n');
            for (auto&& action : cont)
            {
                msg.append_raw(request_type_indent(action->request_type)).append_raw(action->spec).append_raw('\n');
            }
            msg::print(msg);
        }
    }

    std::string create_export_id()
    {
        const tm date_time = get_current_date_time_local();

        // Format is: YYYYmmdd-HHMMSS
        // 15 characters + 1 null terminating character will be written for a total of 16 chars
        char mbstr[16];
        const size_t bytes_written = std::strftime(mbstr, sizeof(mbstr), "%Y%m%d-%H%M%S", &date_time);
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               bytes_written == 15,
                               msgUnexpectedByteSize,
                               msg::expected = "15",
                               msg::actual = bytes_written);
        const std::string date_time_as_string(mbstr);
        return ("vcpkg-export-" + date_time_as_string);
    }

    Path do_nuget_export(const VcpkgPaths& paths,
                         const std::string& nuget_id,
                         const std::string& nuget_version,
                         const std::string& nuget_description,
                         const Path& raw_exported_dir,
                         const Path& output_dir)
    {
        const Filesystem& fs = paths.get_filesystem();
        fs.create_directories(paths.buildsystems / "tmp", IgnoreErrors{});

        // This file will be placed in "build\native" in the nuget package. Therefore, go up two dirs.
        const std::string targets_redirect_content =
            create_targets_redirect("$(MSBuildThisFileDirectory)../../scripts/buildsystems/msbuild/vcpkg.targets");
        const auto targets_redirect = paths.buildsystems / "tmp" / "vcpkg.export.nuget.targets";
        fs.write_contents(targets_redirect, targets_redirect_content, VCPKG_LINE_INFO);

        // This file will be placed in "build\native" in the nuget package. Therefore, go up two dirs.
        const std::string props_redirect_content =
            create_targets_redirect("$(MSBuildThisFileDirectory)../../scripts/buildsystems/msbuild/vcpkg.props");
        const auto props_redirect = paths.buildsystems / "tmp" / "vcpkg.export.nuget.props";
        fs.write_contents(props_redirect, props_redirect_content, VCPKG_LINE_INFO);

        const std::string nuspec_file_content = create_nuspec_file_contents(
            raw_exported_dir, targets_redirect, props_redirect, nuget_id, nuget_version, nuget_description);
        const auto nuspec_file_path = paths.buildsystems / "tmp" / "vcpkg.export.nuspec";
        fs.write_contents(nuspec_file_path, nuspec_file_content, VCPKG_LINE_INFO);

        // -NoDefaultExcludes is needed for ".vcpkg-root"
        Command cmd;
#ifndef _WIN32
        cmd.string_arg(paths.get_tool_exe(Tools::MONO, out_sink));
#endif
        cmd.string_arg(paths.get_tool_exe(Tools::NUGET, out_sink))
            .string_arg("pack")
            .string_arg(nuspec_file_path)
            .string_arg("-OutputDirectory")
            .string_arg(output_dir)
            .string_arg("-NoDefaultExcludes");

        RedirectedProcessLaunchSettings settings;
        settings.environment = get_clean_environment();
        return flatten(cmd_execute_and_capture_output(cmd, settings), Tools::NUGET)
            .map([&](Unit) { return output_dir / (nuget_id + "." + nuget_version + ".nupkg"); })
            .value_or_exit(VCPKG_LINE_INFO);
    }

    struct ArchiveFormat final
    {
        enum class BackingEnum
        {
            ZIP = 1,
            SEVEN_ZIP,
        };

        constexpr ArchiveFormat() = delete;

        constexpr ArchiveFormat(BackingEnum backing_enum, StringLiteral extension, StringLiteral cmake_option)
            : backing_enum(backing_enum), m_extension(extension), m_cmake_option(cmake_option)
        {
        }

        constexpr operator BackingEnum() const { return backing_enum; }
        constexpr StringLiteral extension() const { return this->m_extension; }
        constexpr StringLiteral cmake_option() const { return this->m_cmake_option; }

    private:
        BackingEnum backing_enum;
        StringLiteral m_extension;
        StringLiteral m_cmake_option;
    };

    namespace ArchiveFormatC
    {
        constexpr const ArchiveFormat ZIP(ArchiveFormat::BackingEnum::ZIP, "zip", "zip");
        constexpr const ArchiveFormat SEVEN_ZIP(ArchiveFormat::BackingEnum::SEVEN_ZIP, "7z", "7zip");
    }

    Path do_archive_export(const VcpkgPaths& paths,
                           const Path& raw_exported_dir,
                           const Path& output_dir,
                           const ArchiveFormat& format)
    {
        const Path& cmake_exe = paths.get_tool_exe(Tools::CMAKE, out_sink);

        const auto exported_dir_filename = raw_exported_dir.filename();
        const auto exported_archive_filename = fmt::format("{}.{}", exported_dir_filename, format.extension());
        const auto exported_archive_path = output_dir / exported_archive_filename;

        auto cmd = Command{cmake_exe}
                       .string_arg("-E")
                       .string_arg("tar")
                       .string_arg("cf")
                       .string_arg(exported_archive_path)
                       .string_arg(Strings::concat("--format=", format.cmake_option()))
                       .string_arg("--")
                       .string_arg(raw_exported_dir);

        ProcessLaunchSettings settings;
        settings.working_directory = raw_exported_dir.parent_path();
        settings.environment = get_clean_environment();
        const auto maybe_exit_code = cmd_execute(cmd, settings);
        Checks::msg_check_exit(
            VCPKG_LINE_INFO, succeeded(maybe_exit_code), msgCreationFailed, msg::path = exported_archive_path);
        return exported_archive_path;
    }

    struct ExportArguments
    {
        bool dry_run = false;
        bool raw = false;
        bool nuget = false;
        bool zip = false;
        bool seven_zip = false;
        bool all_installed = false;
        bool dereference_symlinks = false;

        Optional<std::string> maybe_output;
        Path output_dir;

        Optional<std::string> maybe_nuget_id;
        Optional<std::string> maybe_nuget_version;
        Optional<std::string> maybe_nuget_description;

        std::vector<PackageSpec> specs;
    };

    constexpr CommandSwitch EXPORT_SWITCHES[] = {
        {SwitchDryRun, msgCmdExportOptDryRun},
        {SwitchRaw, msgCmdExportOptRaw},
        {SwitchNuGet, msgCmdExportOptNuget},
        {SwitchZip, msgCmdExportOptZip},
        {SwitchSevenZip, msgCmdExportOpt7Zip},
        {SwitchXAllInstalled, msgCmdExportOptInstalled},
        {SwitchDereferenceSymlinks, msgCmdExportOptDereferenceSymlinks},
    };

    constexpr CommandSetting EXPORT_SETTINGS[] = {
        {SwitchOutput, msgCmdExportSettingOutput},
        {SwitchOutputDir, msgCmdExportSettingOutputDir},
        {SwitchNuGetId, msgCmdExportSettingNugetID},
        {SwitchNuGetDescription, msgCmdExportSettingNugetDesc},
        {SwitchNuGetVersion, msgCmdExportSettingNugetVersion},
    };

    ExportArguments handle_export_command_arguments(const VcpkgPaths& paths,
                                                    const VcpkgCmdArguments& args,
                                                    Triplet default_triplet,
                                                    const StatusParagraphs& status_db)
    {
        ExportArguments ret;

        const auto options = args.parse_arguments(CommandExportMetadata);

        ret.dry_run = Util::Sets::contains(options.switches, SwitchDryRun);
        ret.raw = Util::Sets::contains(options.switches, SwitchRaw);
        ret.nuget = Util::Sets::contains(options.switches, SwitchNuGet);
        ret.zip = Util::Sets::contains(options.switches, SwitchZip);
        ret.seven_zip = Util::Sets::contains(options.switches, SwitchSevenZip);
        ret.maybe_output = Util::lookup_value_copy(options.settings, SwitchOutput);
        ret.all_installed = Util::Sets::contains(options.switches, SwitchXAllInstalled);
        ret.dereference_symlinks = Util::Sets::contains(options.switches, SwitchDereferenceSymlinks);

        if (paths.manifest_mode_enabled())
        {
            auto output_dir_opt = Util::lookup_value(options.settings, SwitchOutputDir);

            // --output-dir is required in manifest mode
            if (auto d = output_dir_opt.get())
            {
                ret.output_dir = paths.original_cwd / *d;
            }
            else
            {
                msg::println_error(msgMissingOption, msg::option = "output-dir");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            // Force enable --all-installed in manifest mode
            ret.all_installed = true;

            // In manifest mode the entire installed directory is exported
            if (!options.command_arguments.empty())
            {
                msg::println_error(msgUnexpectedArgument, msg::option = options.command_arguments[0]);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        ret.output_dir = ret.output_dir.empty() ? Util::lookup_value(options.settings, SwitchOutputDir)
                                                      .map([&](const Path& p) { return paths.original_cwd / p; })
                                                      .value_or(paths.root)
                                                : ret.output_dir;

        if (ret.all_installed)
        {
            auto installed_ipv = get_installed_ports(status_db);
            std::transform(installed_ipv.begin(),
                           installed_ipv.end(),
                           std::back_inserter(ret.specs),
                           [](const auto& ipv) { return ipv.spec(); });
        }
        else
        {
            // input sanitization
            ret.specs = Util::fmap(options.command_arguments, [&](auto&& arg) {
                return parse_package_spec(arg, default_triplet).value_or_exit(VCPKG_LINE_INFO);
            });
        }

        if (!ret.raw && !ret.nuget && !ret.zip && !ret.seven_zip && !ret.dry_run)
        {
            msg::println_error(msgProvideExportType);
            msg::write_unlocalized_text(Color::none, CommandExportMetadata.get_example_text());
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        struct OptionPair
        {
            const StringLiteral& name;
            Optional<std::string>& out_opt;
        };
        const auto options_implies = [&](const StringLiteral& main_opt_name,
                                         bool is_main_opt,
                                         const std::initializer_list<OptionPair>& implying_opts) {
            if (is_main_opt)
            {
                for (auto&& opt : implying_opts)
                    opt.out_opt = Util::lookup_value_copy(options.settings, opt.name);
            }
            else
            {
                for (auto&& opt : implying_opts)
                    Checks::msg_check_exit(VCPKG_LINE_INFO,
                                           !Util::Maps::contains(options.settings, opt.name),
                                           msgMutuallyExclusiveOption,
                                           msg::value = opt.name,
                                           msg::option = main_opt_name);
            }
        };

        options_implies(SwitchNuGet,
                        ret.nuget,
                        {
                            {SwitchNuGetId, ret.maybe_nuget_id},
                            {SwitchNuGetVersion, ret.maybe_nuget_version},
                            {SwitchNuGetDescription, ret.maybe_nuget_description},
                        });

        return ret;
    }

    void print_next_step_info(const Path& prefix)
    {
        const auto cmake_toolchain = prefix / "scripts/buildsystems/vcpkg.cmake";
        const CMakeVariable cmake_variable = CMakeVariable("CMAKE_TOOLCHAIN_FILE", cmake_toolchain.generic_u8string());
        msg::println(msg::format(msgCMakeUsingExportedLibs, msg::value = cmake_variable.s));
    }

    void handle_raw_based_export(Span<const ExportPlanAction> export_plan,
                                 const ExportArguments& opts,
                                 const std::string& export_id,
                                 const VcpkgPaths& paths)
    {
        const Filesystem& fs = paths.get_filesystem();
        const auto raw_exported_dir_path = opts.output_dir / export_id;
        fs.remove_all(raw_exported_dir_path, VCPKG_LINE_INFO);

        // TODO: error handling
        fs.create_directory(raw_exported_dir_path, IgnoreErrors{});

        // execute the plan
        {
            const InstalledPaths export_paths(raw_exported_dir_path / "installed");
            for (const ExportPlanAction& action : export_plan)
            {
                if (action.plan_type != ExportPlanType::ALREADY_BUILT)
                {
                    Checks::unreachable(VCPKG_LINE_INFO);
                }

                msg::println(msgExportingPackage, msg::package_name = action.spec);

                const BinaryParagraph& binary_paragraph = action.core_paragraph().value_or_exit(VCPKG_LINE_INFO);
                const auto& triplet_canonical_name = action.spec.triplet().canonical_name();

                auto lines =
                    fs.read_lines(paths.installed().listfile_path(binary_paragraph)).value_or_exit(VCPKG_LINE_INFO);
                auto proximate_files = convert_list_to_proximate_files(std::move(lines), triplet_canonical_name);
                install_files_and_write_listfile(fs,
                                                 paths.installed().triplet_dir(action.spec.triplet()),
                                                 proximate_files,
                                                 export_paths.root(),
                                                 triplet_canonical_name,
                                                 export_paths.listfile_path(binary_paragraph),
                                                 opts.dereference_symlinks ? SymlinkHydrate::CopyData
                                                                           : SymlinkHydrate::CopySymlinks);
            }
        }

        // Copy files needed for integration
        export_integration_files(raw_exported_dir_path, paths);

        if (opts.raw)
        {
            msg::println(Color::success, msgFilesExported, msg::path = raw_exported_dir_path);
            print_next_step_info(raw_exported_dir_path);
        }

        if (opts.nuget)
        {
            const auto nuget_id = opts.maybe_nuget_id.value_or(raw_exported_dir_path.filename().to_string());
            const auto nuget_version = opts.maybe_nuget_version.value_or("1.0.0");
            const auto nuget_description = opts.maybe_nuget_description.value_or("Vcpkg NuGet export");

            msg::println(msgCreatingNugetPackage);

            const auto output_path = do_nuget_export(
                paths, nuget_id, nuget_version, nuget_description, raw_exported_dir_path, opts.output_dir);

            msg::println(Color::success, msgCreatedNuGetPackage, msg::path = output_path);
            msg::println(msgInstallPackageInstruction, msg::value = nuget_id, msg::path = output_path.parent_path());
        }

        if (opts.zip)
        {
            msg::println(msgCreatingZipArchive);
            const auto output_path =
                do_archive_export(paths, raw_exported_dir_path, opts.output_dir, ArchiveFormatC::ZIP);
            msg::println(Color::success, msgExportedZipArchive, msg::path = output_path);
            print_next_step_info("[...]");
        }

        if (opts.seven_zip)
        {
            msg::println(msgCreating7ZipArchive);
            const auto output_path =
                do_archive_export(paths, raw_exported_dir_path, opts.output_dir, ArchiveFormatC::SEVEN_ZIP);
            msg::println(Color::success, msgExported7zipArchive, msg::path = output_path);
            print_next_step_info("[...]");
        }

        if (!opts.raw)
        {
            fs.remove_all(raw_exported_dir_path, VCPKG_LINE_INFO);
        }
    }
} // unnamed namespace

namespace vcpkg
{
    void export_integration_files(const Path& raw_exported_dir_path, const VcpkgPaths& paths)
    {
        const std::vector<Path> integration_files_relative_to_root = {
            Path{"scripts/buildsystems/msbuild/applocal.ps1"},
            Path{"scripts/buildsystems/msbuild/vcpkg.targets"},
            Path{"scripts/buildsystems/msbuild/vcpkg.props"},
            Path{"scripts/buildsystems/msbuild/vcpkg-general.xml"},
            Path{"scripts/buildsystems/vcpkg.cmake"},
            Path{"scripts/buildsystems/osx/applocal.py"},
            Path{"scripts/cmake/vcpkg_get_windows_sdk.cmake"},
        };

        const Filesystem& fs = paths.get_filesystem();
        for (const Path& file : integration_files_relative_to_root)
        {
            const auto source = paths.root / file;
            auto destination = raw_exported_dir_path / file;
            fs.create_directories(destination.parent_path(), IgnoreErrors{});
            fs.copy_file(source, destination, CopyOptions::overwrite_existing, VCPKG_LINE_INFO);
        }

        // Copying exe (this is not relative to root)
        Path vcpkg_exe = get_exe_path_of_current_process();
#if defined(_WIN32)
        auto destination = raw_exported_dir_path / "vcpkg.exe";
#else
        auto destination = raw_exported_dir_path / "vcpkg";
#endif
        fs.copy_file(vcpkg_exe, destination, CopyOptions::overwrite_existing, VCPKG_LINE_INFO);

        fs.write_contents(raw_exported_dir_path / ".vcpkg-root", "", VCPKG_LINE_INFO);
    }

    constexpr CommandMetadata CommandExportMetadata{
        "export",
        msgCmdExportSynopsis,
        {msgCmdExportExample1, "vcpkg export zlib zlib:x64-windows boost --nuget"},
        "https://learn.microsoft.com/vcpkg/commands/export",
        AutocompletePriority::Public,
        0,
        SIZE_MAX,
        {EXPORT_SWITCHES, EXPORT_SETTINGS},
        nullptr,
    };

    void command_export_and_exit(const VcpkgCmdArguments& args,
                                 const VcpkgPaths& paths,
                                 Triplet default_triplet,
                                 Triplet host_triplet)
    {
        (void)host_triplet;
        const StatusParagraphs status_db = database_load(paths.get_filesystem(), paths.installed());
        const auto opts = handle_export_command_arguments(paths, args, default_triplet, status_db);

        // Load ports from ports dirs
        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set, make_overlay_provider(fs, paths.overlay_ports));

        // create the plan
        std::vector<ExportPlanAction> export_plan = create_export_plan(opts.specs, status_db);
        if (export_plan.empty())
        {
            msg::println_error(msgCmdExportEmptyPlan);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        std::map<ExportPlanType, std::vector<const ExportPlanAction*>> group_by_plan_type;
        Util::group_by(export_plan, &group_by_plan_type, [](const ExportPlanAction& p) { return p.plan_type; });
        print_export_plan(group_by_plan_type);

        const bool has_non_user_requested_packages =
            Util::any_of(export_plan, [](const ExportPlanAction& package) -> bool {
                return package.request_type != RequestType::USER_REQUESTED;
            });

        if (has_non_user_requested_packages)
        {
            msg::println(Color::warning, msgAdditionalPackagesToExport);
        }

        const auto it = group_by_plan_type.find(ExportPlanType::NOT_BUILT);
        if (it != group_by_plan_type.cend() && !it->second.empty())
        {
            // No need to show all of them, just the user-requested ones. Dependency resolution will handle the rest.
            std::vector<const ExportPlanAction*> unbuilt = it->second;
            Util::erase_remove_if(
                unbuilt, [](const ExportPlanAction* a) { return a->request_type != RequestType::USER_REQUESTED; });

            const auto s = Strings::join(" ", unbuilt, [](const ExportPlanAction* a) { return a->spec.to_string(); });
            msg::println(msg::format(msgPrebuiltPackages).append_raw('\n').append_raw("vcpkg install ").append_raw(s));
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        if (opts.dry_run)
        {
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        std::string export_id = opts.maybe_output.value_or(create_export_id());

        if (opts.raw || opts.nuget || opts.zip || opts.seven_zip)
        {
            handle_raw_based_export(export_plan, opts, export_id, paths);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    std::vector<std::string> convert_list_to_proximate_files(std::vector<std::string>&& lines,
                                                             StringView triplet_canonical_name)
    {
        auto prefix_length = triplet_canonical_name.size() + 1; // +1 for the trailing '/'
        std::vector<std::string> proximate_files;
        for (auto&& suffix : lines)
        {
            if (suffix.size() <= prefix_length)
            {
                continue;
            }

            suffix.erase(0, prefix_length);
            if (suffix.back() == '/') suffix.pop_back();
            if (suffix.empty()) continue;
            proximate_files.emplace_back(std::move(suffix));
        }

        return proximate_files;
    }
} // namespace vcpkg
