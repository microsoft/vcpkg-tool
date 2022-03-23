#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/commands.h>
#include <vcpkg/export.h>
#include <vcpkg/export.ifw.h>
#include <vcpkg/install.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Export::IFW
{
    using Dependencies::ExportPlanAction;
    using Dependencies::ExportPlanType;
    using Install::InstallDir;

    // requires: after_prefix <= semi
    // requires: *semi == ';'
    static bool is_character_ref(const char* after_prefix, const char* semi)
    {
        if (after_prefix == semi)
        {
            return false;
        }

        if (*after_prefix == '#')
        {
            ++after_prefix;
            if (*after_prefix == 'x')
            {
                ++after_prefix;
                // hex character escape: &#xABC;
                return after_prefix != semi && std::all_of(after_prefix, semi, ParserBase::is_hex_digit);
            }

            // decimal character escape: &#123;
            return after_prefix != semi && std::all_of(after_prefix, semi, ParserBase::is_ascii_digit);
        }

        // word character escape: &amp;
        return std::all_of(after_prefix, semi, ParserBase::is_word_char);
    }

    std::string safe_rich_from_plain_text(StringView text)
    {
        // looking for `&`, not followed by:
        // - '#<numbers>;`
        // - '#x<hex numbers>;`
        // - `<numbers, letters, or _>;`
        // (basically, an HTML character entity reference)
        constexpr static StringLiteral escaped_amp = "&amp;";

        auto first = text.begin();
        const auto last = text.end();

        std::string result;
        for (;;)
        {
            auto amp = std::find(first, last, '&');
            result.append(first, amp);
            first = amp;
            if (first == last)
            {
                break;
            }

            ++first; // skip amp
            if (first == last)
            {
                result.append(escaped_amp);
                break;
            }
            else
            {
                auto semi = std::find(first, last, ';');

                if (semi != last && is_character_ref(first, semi))
                {
                    first = amp;
                }
                else
                {
                    result.append(escaped_amp.begin(), escaped_amp.end());
                }
                result.append(first, semi);
                first = semi;
            }
        }
        return result;
    }

    namespace
    {
        std::string create_release_date()
        {
            const tm date_time = get_current_date_time_local();

            // Format is: YYYY-mm-dd
            // 10 characters + 1 null terminating character will be written for a total of 11 chars
            char mbstr[11];
            const size_t bytes_written = std::strftime(mbstr, sizeof(mbstr), "%Y-%m-%d", &date_time);
            Checks::check_exit(VCPKG_LINE_INFO,
                               bytes_written == 10,
                               "Expected 10 bytes to be written, but %u were written",
                               bytes_written);
            const std::string date_time_as_string(mbstr);
            return date_time_as_string;
        }

        Path get_packages_dir_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            return ifw_options.maybe_packages_dir_path.has_value()
                       ? Path(ifw_options.maybe_packages_dir_path.value_or_exit(VCPKG_LINE_INFO))
                       : paths.root / (export_id + "-ifw-packages");
        }

        Path get_repository_dir_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            return ifw_options.maybe_repository_dir_path.has_value()
                       ? Path(ifw_options.maybe_repository_dir_path.value_or_exit(VCPKG_LINE_INFO))
                       : paths.root / (export_id + "-ifw-repository");
        }

        Path get_config_file_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            return ifw_options.maybe_config_file_path.has_value()
                       ? Path(ifw_options.maybe_config_file_path.value_or_exit(VCPKG_LINE_INFO))
                       : paths.root / (export_id + "-ifw-configuration.xml");
        }

        Path get_installer_file_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            return ifw_options.maybe_installer_file_path.has_value()
                       ? Path(ifw_options.maybe_installer_file_path.value_or_exit(VCPKG_LINE_INFO))
                       : paths.root / (export_id + "-ifw-installer.exe");
        }

        Path export_real_package(const Path& ifw_packages_dir_path, const ExportPlanAction& action, Filesystem& fs)
        {
            std::error_code ec;

            const BinaryParagraph& binary_paragraph = action.core_paragraph().value_or_exit(VCPKG_LINE_INFO);

            // Prepare meta dir
            const auto package_xml_dir_path =
                ifw_packages_dir_path /
                Strings::format("packages.%s.%s/meta", action.spec.name(), action.spec.triplet().canonical_name());
            const auto package_xml_file_path = package_xml_dir_path / "package.xml";
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(
                VCPKG_LINE_INFO, !ec, "Could not create directory for package file %s", package_xml_file_path);

            auto deps = Strings::join(
                ",", binary_paragraph.dependencies, [](const auto& dep) { return "packages." + dep.name() + ":"; });

            if (!deps.empty()) deps = "\n    <Dependencies>" + deps + "</Dependencies>";

            fs.write_contents(package_xml_file_path,
                              Strings::format(
                                  R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>%s</DisplayName>
    <Version>%s</Version>
    <ReleaseDate>%s</ReleaseDate>
    <AutoDependOn>packages.%s:,triplets.%s:</AutoDependOn>%s
    <Virtual>true</Virtual>
</Package>
)###",
                                  action.spec.to_string(),
                                  binary_paragraph.version,
                                  create_release_date(),
                                  action.spec.name(),
                                  action.spec.triplet().canonical_name(),
                                  deps),
                              VCPKG_LINE_INFO);

            // Return dir path for export package data
            return ifw_packages_dir_path / Strings::format("packages.%s.%s/data/installed",
                                                           action.spec.name(),
                                                           action.spec.triplet().canonical_name());
        }

        void export_unique_packages(const Path& raw_exported_dir_path,
                                    std::map<std::string, const ExportPlanAction*> unique_packages,
                                    Filesystem& fs)
        {
            std::error_code ec;

            // packages

            auto package_xml_dir_path = raw_exported_dir_path / "packages/meta";
            auto package_xml_file_path = package_xml_dir_path / "package.xml";
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(
                VCPKG_LINE_INFO, !ec, "Could not create directory for package file %s", package_xml_file_path);
            fs.write_contents(package_xml_file_path,
                              Strings::format(
                                  R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>Packages</DisplayName>
    <Version>1.0.0</Version>
    <ReleaseDate>%s</ReleaseDate>
</Package>
)###",
                                  create_release_date()),
                              VCPKG_LINE_INFO);

            for (const auto& unique_package : unique_packages)
            {
                const ExportPlanAction& action = *(unique_package.second);
                const BinaryParagraph& binary_paragraph = action.core_paragraph().value_or_exit(VCPKG_LINE_INFO);

                package_xml_dir_path = raw_exported_dir_path / Strings::format("packages.%s", unique_package.first);
                package_xml_file_path = package_xml_dir_path / "export_integration_files";
                fs.create_directories(package_xml_dir_path, ec);
                Checks::check_exit(
                    VCPKG_LINE_INFO, !ec, "Could not create directory for package file %s", package_xml_file_path);

                fs.write_contents(package_xml_file_path,
                                  Strings::format(
                                      R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>%s</DisplayName>
    <Description>%s</Description>
    <Version>%s</Version>
    <ReleaseDate>%s</ReleaseDate>
</Package>
)###",
                                      action.spec.name(),
                                      safe_rich_from_plain_text(Strings::join("\n", binary_paragraph.description)),
                                      binary_paragraph.version,
                                      create_release_date()),
                                  VCPKG_LINE_INFO);
            }
        }

        void export_unique_triplets(const Path& raw_exported_dir_path,
                                    std::set<std::string> unique_triplets,
                                    Filesystem& fs)
        {
            std::error_code ec;

            // triplets

            auto package_xml_dir_path = raw_exported_dir_path / "triplets/meta";
            auto package_xml_file_path = package_xml_dir_path / "package.xml";
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(
                VCPKG_LINE_INFO, !ec, "Could not create directory for package file %s", package_xml_file_path);
            fs.write_contents(package_xml_file_path,
                              Strings::format(
                                  R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>Triplets</DisplayName>
    <Version>1.0.0</Version>
    <ReleaseDate>%s</ReleaseDate>
</Package>
)###",
                                  create_release_date()),
                              VCPKG_LINE_INFO);

            for (const std::string& triplet : unique_triplets)
            {
                package_xml_dir_path = raw_exported_dir_path / Strings::format("triplets.%s/meta", triplet);
                package_xml_file_path = raw_exported_dir_path / "package.xml";
                fs.create_directories(package_xml_dir_path, ec);
                Checks::check_exit(
                    VCPKG_LINE_INFO, !ec, "Could not create directory for package file %s", package_xml_file_path);
                fs.write_contents(package_xml_file_path,
                                  Strings::format(
                                      R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>%s</DisplayName>
    <Version>1.0.0</Version>
    <ReleaseDate>%s</ReleaseDate>
</Package>
)###",
                                      triplet,
                                      create_release_date()),
                                  VCPKG_LINE_INFO);
            }
        }

        void export_integration(const Path& raw_exported_dir_path, Filesystem& fs)
        {
            std::error_code ec;

            // integration
            auto package_xml_dir_path = raw_exported_dir_path / "integration/meta";
            auto package_xml_file_path = package_xml_dir_path / "package.xml";
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(
                VCPKG_LINE_INFO, !ec, "Could not create directory for package file %s", package_xml_file_path);

            fs.write_contents(package_xml_file_path,
                              Strings::format(
                                  R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>Integration</DisplayName>
    <Version>1.0.0</Version>
    <ReleaseDate>%s</ReleaseDate>
</Package>
)###",
                                  create_release_date()),
                              VCPKG_LINE_INFO);
        }

        void export_config(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            std::error_code ec;
            Filesystem& fs = paths.get_filesystem();

            const auto config_xml_file_path = get_config_file_path(export_id, ifw_options, paths);
            fs.create_directories(config_xml_file_path.parent_path(), ec);
            Checks::check_exit(
                VCPKG_LINE_INFO, !ec, "Could not create directory for configuration file %s", config_xml_file_path);

            std::string formatted_repo_url;
            std::string ifw_repo_url = ifw_options.maybe_repository_url.value_or("");
            if (!ifw_repo_url.empty())
            {
                formatted_repo_url = Strings::format(R"###(
    <RemoteRepositories>
        <Repository>
            <Url>%s</Url>
        </Repository>
    </RemoteRepositories>)###",
                                                     ifw_repo_url);
            }

            fs.write_contents(config_xml_file_path,
                              Strings::format(
                                  R"###(<?xml version="1.0"?>
<Installer>
    <Name>vcpkg</Name>
    <Version>1.0.0</Version>
    <StartMenuDir>vcpkg</StartMenuDir>
    <TargetDir>@RootDir@/src/vcpkg</TargetDir>%s
</Installer>
)###",
                                  formatted_repo_url),
                              VCPKG_LINE_INFO);
        }

        void export_maintenance_tool(const Path& ifw_packages_dir_path, const VcpkgPaths& paths)
        {
            print2("Exporting maintenance tool...\n");

            std::error_code ec;
            Filesystem& fs = paths.get_filesystem();

            const Path& installerbase_exe = paths.get_tool_exe(Tools::IFW_INSTALLER_BASE);
            auto tempmaintenancetool_dir = ifw_packages_dir_path / "maintenance/data";
            auto tempmaintenancetool = tempmaintenancetool_dir / "tempmaintenancetool.exe";
            fs.create_directories(tempmaintenancetool_dir, ec);
            Checks::check_exit(
                VCPKG_LINE_INFO, !ec, "Could not create directory for package file %s", tempmaintenancetool);
            fs.copy_file(installerbase_exe, tempmaintenancetool, CopyOptions::overwrite_existing, ec);
            Checks::check_exit(VCPKG_LINE_INFO, !ec, "Could not write package file %s", tempmaintenancetool);

            auto package_xml_dir_path = ifw_packages_dir_path / "maintenance/meta";
            auto package_xml_file_path = package_xml_dir_path / "package.xml";
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(
                VCPKG_LINE_INFO, !ec, "Could not create directory for package file %s", package_xml_file_path);
            fs.write_contents(package_xml_file_path,
                              Strings::format(
                                  R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>Maintenance Tool</DisplayName>
    <Description>Maintenance Tool</Description>
    <Version>1.0.0</Version>
    <ReleaseDate>%s</ReleaseDate>
    <Script>maintenance.qs</Script>
    <Essential>true</Essential>
    <Virtual>true</Virtual>
    <ForcedInstallation>true</ForcedInstallation>
</Package>
)###",
                                  create_release_date()),
                              VCPKG_LINE_INFO);
            const auto script_source = paths.root / "scripts" / "ifw" / "maintenance.qs";
            const auto script_destination = ifw_packages_dir_path / "maintenance" / "meta" / "maintenance.qs";
            fs.copy_file(script_source, script_destination, CopyOptions::overwrite_existing, ec);
            Checks::check_exit(VCPKG_LINE_INFO, !ec, "Could not write package file %s", script_destination);

            print2("Exporting maintenance tool... done\n");
        }

        void do_repository(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            const Path& repogen_exe = paths.get_tool_exe(Tools::IFW_REPOGEN);
            const auto packages_dir = get_packages_dir_path(export_id, ifw_options, paths);
            const auto repository_dir = get_repository_dir_path(export_id, ifw_options, paths);

            print2("Generating repository ", repository_dir, "...\n");

            std::error_code ec;
            Path failure_point;
            Filesystem& fs = paths.get_filesystem();

            fs.remove_all(repository_dir, ec, failure_point);
            Checks::check_exit(VCPKG_LINE_INFO,
                               !ec,
                               "Could not remove outdated repository directory %s due to file %s",
                               repository_dir,
                               failure_point);

            auto cmd_line =
                Command(repogen_exe).string_arg("--packages").string_arg(packages_dir).string_arg(repository_dir);

            const int exit_code =
                cmd_execute_and_capture_output(cmd_line, default_working_directory, get_clean_environment()).exit_code;
            Checks::check_exit(VCPKG_LINE_INFO, exit_code == 0, "Error: IFW repository generating failed");

            vcpkg::printf(Color::success, "Generating repository %s... done.\n", repository_dir);
        }

        void do_installer(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            const Path& binarycreator_exe = paths.get_tool_exe(Tools::IFW_BINARYCREATOR);
            const auto config_file = get_config_file_path(export_id, ifw_options, paths);
            const auto packages_dir = get_packages_dir_path(export_id, ifw_options, paths);
            const auto repository_dir = get_repository_dir_path(export_id, ifw_options, paths);
            const auto installer_file = get_installer_file_path(export_id, ifw_options, paths);

            vcpkg::printf("Generating installer %s...\n", installer_file);

            Command cmd_line;

            std::string ifw_repo_url = ifw_options.maybe_repository_url.value_or("");
            if (!ifw_repo_url.empty())
            {
                cmd_line = Command(binarycreator_exe)
                               .string_arg("--online-only")
                               .string_arg("--config")
                               .string_arg(config_file)
                               .string_arg("--repository")
                               .string_arg(repository_dir)
                               .string_arg(installer_file);
            }
            else
            {
                cmd_line = Command(binarycreator_exe)
                               .string_arg("--config")
                               .string_arg(config_file)
                               .string_arg("--packages")
                               .string_arg(packages_dir)
                               .string_arg(installer_file);
            }

            const int exit_code =
                cmd_execute_and_capture_output(cmd_line, default_working_directory, get_clean_environment()).exit_code;
            Checks::check_exit(VCPKG_LINE_INFO, exit_code == 0, "Error: IFW installer generating failed");

            vcpkg::printf(Color::success, "Generating installer %s... done.\n", installer_file);
        }
    }

    void do_export(const std::vector<ExportPlanAction>& export_plan,
                   const std::string& export_id,
                   const Options& ifw_options,
                   const VcpkgPaths& paths)
    {
        std::error_code ec;
        Path failure_point;
        Filesystem& fs = paths.get_filesystem();

        // Prepare packages directory
        const auto ifw_packages_dir_path = get_packages_dir_path(export_id, ifw_options, paths);

        fs.remove_all(ifw_packages_dir_path, ec, failure_point);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Could not remove outdated packages directory %s due to file %s",
                           ifw_packages_dir_path,
                           failure_point);

        fs.create_directory(ifw_packages_dir_path, ec);
        Checks::check_exit(VCPKG_LINE_INFO, !ec, "Could not create packages directory %s", ifw_packages_dir_path);

        // Export maintenance tool
        export_maintenance_tool(ifw_packages_dir_path, paths);

        vcpkg::printf("Exporting packages %s...\n", ifw_packages_dir_path);

        // execute the plan
        std::map<std::string, const ExportPlanAction*> unique_packages;
        std::set<std::string> unique_triplets;
        for (const ExportPlanAction& action : export_plan)
        {
            if (action.plan_type != ExportPlanType::ALREADY_BUILT)
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }

            print2("Exporting package ", action.spec, "...\n");

            const BinaryParagraph& binary_paragraph = action.core_paragraph().value_or_exit(VCPKG_LINE_INFO);

            unique_packages[action.spec.name()] = &action;
            unique_triplets.insert(action.spec.triplet().canonical_name());

            // Export real package and return data dir for installation
            const InstalledPaths installed(export_real_package(ifw_packages_dir_path, action, fs));

            // Copy package data
            const InstallDir dirs =
                InstallDir::from_destination_root(installed, action.spec.triplet(), binary_paragraph);

            Install::install_package_and_write_listfile(fs, paths.package_dir(action.spec), dirs);
        }

        vcpkg::printf("Exporting packages %s... done\n", ifw_packages_dir_path);

        const auto config_file = get_config_file_path(export_id, ifw_options, paths);

        vcpkg::printf("Generating configuration %s...\n", config_file);

        // Unique packages
        export_unique_packages(ifw_packages_dir_path, unique_packages, fs);

        // Unique triplets
        export_unique_triplets(ifw_packages_dir_path, unique_triplets, fs);

        // Copy files needed for integration
        export_integration_files(ifw_packages_dir_path / "integration" / "data", paths);
        // Integration
        export_integration(ifw_packages_dir_path, fs);

        // Configuration
        export_config(export_id, ifw_options, paths);

        vcpkg::printf("Generating configuration %s... done.\n", config_file);

        // Do repository (optional)
        std::string ifw_repo_url = ifw_options.maybe_repository_url.value_or("");
        if (!ifw_repo_url.empty())
        {
            do_repository(export_id, ifw_options, paths);
        }

        // Do installer
        do_installer(export_id, ifw_options, paths);
    }
}
