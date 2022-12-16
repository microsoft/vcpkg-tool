#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/commands.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/export.chocolatey.h>
#include <vcpkg/export.h>
#include <vcpkg/export.ifw.h>
#include <vcpkg/export.prefab.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/install.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Export
{
    static std::string create_nuspec_file_contents(const Path& raw_exported_dir,
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

    static std::string create_targets_redirect(const std::string& target_path) noexcept
    {
        return Strings::format(R"###(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Condition="Exists('%s')" Project="%s" />
</Project>
)###",
                               target_path,
                               target_path);
    }

    static void print_plan(const std::map<ExportPlanType, std::vector<const ExportPlanAction*>>& group_by_plan_type)
    {
        static constexpr std::array<ExportPlanType, 2> ORDER = {ExportPlanType::ALREADY_BUILT,
                                                                ExportPlanType::NOT_BUILT};
        for (const ExportPlanType plan_type : ORDER)
        {
            const auto it = group_by_plan_type.find(plan_type);
            if (it == group_by_plan_type.cend())
            {
                continue;
            }

            std::vector<const ExportPlanAction*> cont = it->second;
            std::sort(cont.begin(), cont.end(), &ExportPlanAction::compare_by_name);
            const std::string as_string = Strings::join("\n", cont, [](const ExportPlanAction* p) {
                return to_output_string(p->request_type, p->spec.to_string(), default_build_package_options);
            });

            switch (plan_type)
            {
                case ExportPlanType::ALREADY_BUILT:
                    msg::println(msg::format(msgExportingAlreadyBuiltPackages).append_raw("\n" + as_string));
                    continue;
                case ExportPlanType::NOT_BUILT:
                    msg::println(msg::format(msgPackagesToInstall).append_raw("\n" + as_string));
                    continue;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }
    }

    static std::string create_export_id()
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

    static Path do_nuget_export(const VcpkgPaths& paths,
                                const std::string& nuget_id,
                                const std::string& nuget_version,
                                const std::string& nuget_description,
                                const Path& raw_exported_dir,
                                const Path& output_dir)
    {
        Filesystem& fs = paths.get_filesystem();
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
        cmd.string_arg(paths.get_tool_exe(Tools::MONO, stdout_sink));
#endif
        cmd.string_arg(paths.get_tool_exe(Tools::NUGET, stdout_sink))
            .string_arg("pack")
            .string_arg(nuspec_file_path)
            .string_arg("-OutputDirectory")
            .string_arg(output_dir)
            .string_arg("-NoDefaultExcludes");

        return flatten(cmd_execute_and_capture_output(cmd, default_working_directory, get_clean_environment()),
                       Tools::NUGET)
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

    static Path do_archive_export(const VcpkgPaths& paths,
                                  const Path& raw_exported_dir,
                                  const Path& output_dir,
                                  const ArchiveFormat& format)
    {
        const Path& cmake_exe = paths.get_tool_exe(Tools::CMAKE, stdout_sink);

        const auto exported_dir_filename = raw_exported_dir.filename();
        const auto exported_archive_filename = Strings::format("%s.%s", exported_dir_filename, format.extension());
        const auto exported_archive_path = output_dir / exported_archive_filename;

        Command cmd;
        cmd.string_arg(cmake_exe)
            .string_arg("-E")
            .string_arg("tar")
            .string_arg("cf")
            .string_arg(exported_archive_path)
            .string_arg(Strings::concat("--format=", format.cmake_option()))
            .string_arg("--")
            .string_arg(raw_exported_dir);

        const int exit_code =
            cmd_execute_clean(cmd, WorkingDirectory{raw_exported_dir.parent_path()}).value_or_exit(VCPKG_LINE_INFO);
        Checks::msg_check_exit(VCPKG_LINE_INFO, exit_code == 0, msgCreationFailed, msg::path = exported_archive_path);
        return exported_archive_path;
    }

    static Optional<std::string> maybe_lookup(std::map<std::string, std::string, std::less<>> const& m, StringView key)
    {
        const auto it = m.find(key);
        if (it != m.end()) return it->second;
        return nullopt;
    }

    void export_integration_files(const Path& raw_exported_dir_path, const VcpkgPaths& paths)
    {
        const std::vector<Path> integration_files_relative_to_root = {
            Path{"./vcpkg.exe"},
            Path{"scripts/buildsystems/msbuild/vcpkg.targets"},
            Path{"scripts/buildsystems/msbuild/vcpkg.props"},
            Path{"scripts/buildsystems/msbuild/vcpkg-general.xml"},
            Path{"scripts/buildsystems/vcpkg.cmake"},
            Path{"scripts/cmake/vcpkg_get_windows_sdk.cmake"},
        };

        Filesystem& fs = paths.get_filesystem();
        for (const Path& file : integration_files_relative_to_root)
        {
            const auto source = paths.root / file;
            auto destination = raw_exported_dir_path / file;
            fs.create_directories(destination.parent_path(), IgnoreErrors{});
            fs.copy_file(source, destination, CopyOptions::overwrite_existing, VCPKG_LINE_INFO);
        }
        fs.write_contents(raw_exported_dir_path / ".vcpkg-root", "", VCPKG_LINE_INFO);
    }

    struct ExportArguments
    {
        bool dry_run = false;
        bool raw = false;
        bool nuget = false;
        bool ifw = false;
        bool zip = false;
        bool seven_zip = false;
        bool chocolatey = false;
        bool prefab = false;
        bool all_installed = false;

        Optional<std::string> maybe_output;
        Path output_dir;

        Optional<std::string> maybe_nuget_id;
        Optional<std::string> maybe_nuget_version;
        Optional<std::string> maybe_nuget_description;

        IFW::Options ifw_options;
        Prefab::Options prefab_options;
        Chocolatey::Options chocolatey_options;
        std::vector<PackageSpec> specs;
    };

    static constexpr StringLiteral OPTION_OUTPUT = "output";
    static constexpr StringLiteral OPTION_OUTPUT_DIR = "output-dir";
    static constexpr StringLiteral OPTION_DRY_RUN = "dry-run";
    static constexpr StringLiteral OPTION_RAW = "raw";
    static constexpr StringLiteral OPTION_NUGET = "nuget";
    static constexpr StringLiteral OPTION_IFW = "ifw";
    static constexpr StringLiteral OPTION_ZIP = "zip";
    static constexpr StringLiteral OPTION_SEVEN_ZIP = "7zip";
    static constexpr StringLiteral OPTION_NUGET_ID = "nuget-id";
    static constexpr StringLiteral OPTION_NUGET_DESCRIPTION = "nuget-description";
    static constexpr StringLiteral OPTION_NUGET_VERSION = "nuget-version";
    static constexpr StringLiteral OPTION_IFW_REPOSITORY_URL = "ifw-repository-url";
    static constexpr StringLiteral OPTION_IFW_PACKAGES_DIR_PATH = "ifw-packages-directory-path";
    static constexpr StringLiteral OPTION_IFW_REPOSITORY_DIR_PATH = "ifw-repository-directory-path";
    static constexpr StringLiteral OPTION_IFW_CONFIG_FILE_PATH = "ifw-configuration-file-path";
    static constexpr StringLiteral OPTION_IFW_INSTALLER_FILE_PATH = "ifw-installer-file-path";
    static constexpr StringLiteral OPTION_CHOCOLATEY = "x-chocolatey";
    static constexpr StringLiteral OPTION_CHOCOLATEY_MAINTAINER = "x-maintainer";
    static constexpr StringLiteral OPTION_CHOCOLATEY_VERSION_SUFFIX = "x-version-suffix";
    static constexpr StringLiteral OPTION_ALL_INSTALLED = "x-all-installed";

    static constexpr StringLiteral OPTION_PREFAB = "prefab";
    static constexpr StringLiteral OPTION_PREFAB_GROUP_ID = "prefab-group-id";
    static constexpr StringLiteral OPTION_PREFAB_ARTIFACT_ID = "prefab-artifact-id";
    static constexpr StringLiteral OPTION_PREFAB_VERSION = "prefab-version";
    static constexpr StringLiteral OPTION_PREFAB_SDK_MIN_VERSION = "prefab-min-sdk";
    static constexpr StringLiteral OPTION_PREFAB_SDK_TARGET_VERSION = "prefab-target-sdk";
    static constexpr StringLiteral OPTION_PREFAB_ENABLE_MAVEN = "prefab-maven";
    static constexpr StringLiteral OPTION_PREFAB_ENABLE_DEBUG = "prefab-debug";

    static constexpr std::array<CommandSwitch, 11> EXPORT_SWITCHES = {{
        {OPTION_DRY_RUN, []() { return msg::format(msgCmdExportOptDryRun); }},
        {OPTION_RAW, []() { return msg::format(msgCmdExportOptRaw); }},
        {OPTION_NUGET, []() { return msg::format(msgCmdExportOptNuget); }},
        {OPTION_IFW, []() { return msg::format(msgCmdExportOptIFW); }},
        {OPTION_ZIP, []() { return msg::format(msgCmdExportOptZip); }},
        {OPTION_SEVEN_ZIP, []() { return msg::format(msgCmdExportOpt7Zip); }},
        {OPTION_CHOCOLATEY, []() { return msg::format(msgCmdExportOptChocolatey); }},
        {OPTION_PREFAB, []() { return msg::format(msgCmdExportOptPrefab); }},
        {OPTION_PREFAB_ENABLE_MAVEN, []() { return msg::format(msgCmdExportOptMaven); }},
        {OPTION_PREFAB_ENABLE_DEBUG, []() { return msg::format(msgCmdExportOptDebug); }},
        {OPTION_ALL_INSTALLED, []() { return msg::format(msgCmdExportOptInstalled); }},
    }};

    static constexpr std::array<CommandSetting, 17> EXPORT_SETTINGS = {{
        {OPTION_OUTPUT, []() { return msg::format(msgCmdExportSettingOutput); }},
        {OPTION_OUTPUT_DIR, []() { return msg::format(msgCmdExportSettingOutputDir); }},
        {OPTION_NUGET_ID, []() { return msg::format(msgCmdExportSettingNugetID); }},
        {OPTION_NUGET_DESCRIPTION, []() { return msg::format(msgCmdExportSettingNugetDesc); }},
        {OPTION_NUGET_VERSION, []() { return msg::format(msgCmdExportSettingNugetVersion); }},
        {OPTION_IFW_REPOSITORY_URL, []() { return msg::format(msgCmdExportSettingRepoURL); }},
        {OPTION_IFW_PACKAGES_DIR_PATH, []() { return msg::format(msgCmdExportSettingPkgDir); }},
        {OPTION_IFW_REPOSITORY_DIR_PATH, []() { return msg::format(msgCmdExportSettingRepoDir); }},
        {OPTION_IFW_CONFIG_FILE_PATH, []() { return msg::format(msgCmdExportSettingConfigFile); }},
        {OPTION_IFW_INSTALLER_FILE_PATH, []() { return msg::format(msgCmdExportSettingInstallerPath); }},
        {OPTION_CHOCOLATEY_MAINTAINER, []() { return msg::format(msgCmdExportSettingChocolateyMaint); }},
        {OPTION_CHOCOLATEY_VERSION_SUFFIX, []() { return msg::format(msgCmdExportSettingChocolateyVersion); }},
        {OPTION_PREFAB_GROUP_ID, []() { return msg::format(msgCmdExportSettingPrefabGroupID); }},
        {OPTION_PREFAB_ARTIFACT_ID, []() { return msg::format(msgCmdExportSettingPrefabArtifactID); }},
        {OPTION_PREFAB_VERSION, []() { return msg::format(msgCmdExportSettingPrefabVersion); }},
        {OPTION_PREFAB_SDK_MIN_VERSION, []() { return msg::format(msgCmdExportSettingSDKMinVersion); }},
        {OPTION_PREFAB_SDK_TARGET_VERSION, []() { return msg::format(msgCmdExportSettingSDKTargetVersion); }},
    }};

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("export zlib zlib:x64-windows boost --nuget"),
        0,
        SIZE_MAX,
        {EXPORT_SWITCHES, EXPORT_SETTINGS},
        nullptr,
    };

    static ExportArguments handle_export_command_arguments(const VcpkgPaths& paths,
                                                           const VcpkgCmdArguments& args,
                                                           Triplet default_triplet,
                                                           const StatusParagraphs& status_db)
    {
        ExportArguments ret;

        const auto options = args.parse_arguments(COMMAND_STRUCTURE);

        ret.dry_run = options.switches.find(OPTION_DRY_RUN) != options.switches.cend();
        ret.raw = options.switches.find(OPTION_RAW) != options.switches.cend();
        ret.nuget = options.switches.find(OPTION_NUGET) != options.switches.cend();
        ret.ifw = options.switches.find(OPTION_IFW) != options.switches.cend();
        ret.zip = options.switches.find(OPTION_ZIP) != options.switches.cend();
        ret.seven_zip = options.switches.find(OPTION_SEVEN_ZIP) != options.switches.cend();
        ret.chocolatey = options.switches.find(OPTION_CHOCOLATEY) != options.switches.cend();
        ret.prefab = options.switches.find(OPTION_PREFAB) != options.switches.cend();
        ret.prefab_options.enable_maven = options.switches.find(OPTION_PREFAB_ENABLE_MAVEN) != options.switches.cend();
        ret.prefab_options.enable_debug = options.switches.find(OPTION_PREFAB_ENABLE_DEBUG) != options.switches.cend();
        ret.maybe_output = maybe_lookup(options.settings, OPTION_OUTPUT);
        auto maybe_output_dir = maybe_lookup(options.settings, OPTION_OUTPUT_DIR);
        if (auto output_dir = maybe_output_dir.get())
        {
            ret.output_dir = paths.original_cwd / *output_dir;
        }
        else
        {
            ret.output_dir = paths.root;
        }
        ret.all_installed = options.switches.find(OPTION_ALL_INSTALLED) != options.switches.end();

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
            ret.specs = Util::fmap(args.command_arguments, [&](auto&& arg) {
                return check_and_get_package_spec(
                    std::string(arg), default_triplet, COMMAND_STRUCTURE.example_text, paths);
            });
        }

        if (!ret.raw && !ret.nuget && !ret.ifw && !ret.zip && !ret.seven_zip && !ret.dry_run && !ret.chocolatey &&
            !ret.prefab)
        {
            msg::println_error(msgProvideExportType);
            msg::write_unlocalized_text_to_stdout(Color::none, COMMAND_STRUCTURE.example_text);
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
                    opt.out_opt = maybe_lookup(options.settings, opt.name);
            }
            else
            {
                for (auto&& opt : implying_opts)
                    Checks::msg_check_exit(VCPKG_LINE_INFO,
                                           !maybe_lookup(options.settings, opt.name),
                                           msgMutuallyExclusiveOption,
                                           msg::value = opt.name,
                                           msg::option = main_opt_name);
            }
        };

        options_implies(OPTION_NUGET,
                        ret.nuget,
                        {
                            {OPTION_NUGET_ID, ret.maybe_nuget_id},
                            {OPTION_NUGET_VERSION, ret.maybe_nuget_version},
                            {OPTION_NUGET_DESCRIPTION, ret.maybe_nuget_description},
                        });

        options_implies(OPTION_IFW,
                        ret.ifw,
                        {
                            {OPTION_IFW_REPOSITORY_URL, ret.ifw_options.maybe_repository_url},
                            {OPTION_IFW_PACKAGES_DIR_PATH, ret.ifw_options.maybe_packages_dir_path},
                            {OPTION_IFW_REPOSITORY_DIR_PATH, ret.ifw_options.maybe_repository_dir_path},
                            {OPTION_IFW_CONFIG_FILE_PATH, ret.ifw_options.maybe_config_file_path},
                            {OPTION_IFW_INSTALLER_FILE_PATH, ret.ifw_options.maybe_installer_file_path},
                        });

        options_implies(OPTION_PREFAB,
                        ret.prefab,
                        {
                            {OPTION_PREFAB_ARTIFACT_ID, ret.prefab_options.maybe_artifact_id},
                            {OPTION_PREFAB_GROUP_ID, ret.prefab_options.maybe_group_id},
                            {OPTION_PREFAB_SDK_MIN_VERSION, ret.prefab_options.maybe_min_sdk},
                            {OPTION_PREFAB_SDK_TARGET_VERSION, ret.prefab_options.maybe_target_sdk},
                            {OPTION_PREFAB_VERSION, ret.prefab_options.maybe_version},
                        });

        options_implies(OPTION_CHOCOLATEY,
                        ret.chocolatey,
                        {
                            {OPTION_CHOCOLATEY_MAINTAINER, ret.chocolatey_options.maybe_maintainer},
                            {OPTION_CHOCOLATEY_VERSION_SUFFIX, ret.chocolatey_options.maybe_version_suffix},
                        });

        return ret;
    }

    static void print_next_step_info(const Path& prefix)
    {
        const auto cmake_toolchain = prefix / "scripts/buildsystems/vcpkg.cmake";
        const CMakeVariable cmake_variable = CMakeVariable("CMAKE_TOOLCHAIN_FILE", cmake_toolchain.generic_u8string());
        msg::println(msg::format(msgCMakeUsingExportedLibs, msg::value = cmake_variable.s));
    }

    static void handle_raw_based_export(Span<const ExportPlanAction> export_plan,
                                        const ExportArguments& opts,
                                        const std::string& export_id,
                                        const VcpkgPaths& paths)
    {
        Filesystem& fs = paths.get_filesystem();
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

                const std::string display_name = action.spec.to_string();
                msg::println(msgExportingPackage, msg::package_name = display_name);

                const BinaryParagraph& binary_paragraph = action.core_paragraph().value_or_exit(VCPKG_LINE_INFO);

                const InstallDir dirs =
                    InstallDir::from_destination_root(export_paths, action.spec.triplet(), binary_paragraph);

                auto lines = fs.read_lines(paths.installed().listfile_path(binary_paragraph), VCPKG_LINE_INFO);
                std::vector<Path> files;
                for (auto&& suffix : lines)
                {
                    if (suffix.empty()) continue;
                    if (suffix.back() == '/') suffix.pop_back();
                    if (suffix == action.spec.triplet().to_string()) continue;
                    files.push_back(paths.installed().root() / suffix);
                }

                install_files_and_write_listfile(fs, paths.installed().triplet_dir(action.spec.triplet()), files, dirs);
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

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths, Triplet default_triplet)
    {
        if (paths.manifest_mode_enabled())
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, msgExportUnsupportedInManifest);
        }
        const StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());
        const auto opts = handle_export_command_arguments(paths, args, default_triplet, status_db);

        // Load ports from ports dirs
        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(
            fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));

        // create the plan
        std::vector<ExportPlanAction> export_plan = create_export_plan(opts.specs, status_db);
        if (export_plan.empty())
        {
            Debug::print("Export plan cannot be empty.");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        std::map<ExportPlanType, std::vector<const ExportPlanAction*>> group_by_plan_type;
        Util::group_by(export_plan, &group_by_plan_type, [](const ExportPlanAction& p) { return p.plan_type; });
        print_plan(group_by_plan_type);

        const bool has_non_user_requested_packages =
            Util::find_if(export_plan, [](const ExportPlanAction& package) -> bool {
                return package.request_type != RequestType::USER_REQUESTED;
            }) != export_plan.cend();

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

        if (opts.ifw)
        {
            IFW::do_export(export_plan, export_id, opts.ifw_options, paths);

            print_next_step_info("@RootDir@/src/vcpkg");
        }

        if (opts.chocolatey)
        {
            Chocolatey::do_export(export_plan, paths, opts.chocolatey_options);
        }

        if (opts.prefab)
        {
            Prefab::do_export(export_plan, paths, opts.prefab_options, default_triplet);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void ExportCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                         const VcpkgPaths& paths,
                                         Triplet default_triplet,
                                         Triplet /*host_triplet*/) const
    {
        Export::perform_and_exit(args, paths, default_triplet);
    }
}
