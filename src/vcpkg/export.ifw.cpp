#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/commands.h>
#include <vcpkg/export.h>
#include <vcpkg/export.ifw.h>
#include <vcpkg/install.h>
#include <vcpkg/tools.h>

namespace vcpkg::Export::IFW
{
    using Dependencies::ExportPlanAction;
    using Dependencies::ExportPlanType;
    using Install::InstallDir;

    namespace
    {
        std::string create_release_date()
        {
            const tm date_time = Chrono::get_current_date_time_local();

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

        std::string safe_rich_from_plain_text(const std::string& text)
        {
            // match standalone ampersand, no HTML number or name
            std::regex standalone_ampersand(R"###(&(?!(#[0-9]+|\w+);))###");

            return std::regex_replace(text, standalone_ampersand, "&amp;");
        }

        path get_packages_dir_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            return ifw_options.maybe_packages_dir_path.has_value()
                       ? path(ifw_options.maybe_packages_dir_path.value_or_exit(VCPKG_LINE_INFO))
                       : paths.root / (export_id + "-ifw-packages");
        }

        path get_repository_dir_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            return ifw_options.maybe_repository_dir_path.has_value()
                       ? path(ifw_options.maybe_repository_dir_path.value_or_exit(VCPKG_LINE_INFO))
                       : paths.root / (export_id + "-ifw-repository");
        }

        path get_config_file_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            return ifw_options.maybe_config_file_path.has_value()
                       ? path(ifw_options.maybe_config_file_path.value_or_exit(VCPKG_LINE_INFO))
                       : paths.root / (export_id + "-ifw-configuration.xml");
        }

        path get_installer_file_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            return ifw_options.maybe_installer_file_path.has_value()
                       ? path(ifw_options.maybe_installer_file_path.value_or_exit(VCPKG_LINE_INFO))
                       : paths.root / (export_id + "-ifw-installer.exe");
        }

        path export_real_package(const path& ifw_packages_dir_path, const ExportPlanAction& action, Filesystem& fs)
        {
            std::error_code ec;

            const BinaryParagraph& binary_paragraph = action.core_paragraph().value_or_exit(VCPKG_LINE_INFO);

            // Prepare meta dir
            const path package_xml_file_path =
                ifw_packages_dir_path /
                Strings::format("packages.%s.%s", action.spec.name(), action.spec.triplet().canonical_name()) / "meta" /
                "package.xml";
            const path package_xml_dir_path = package_xml_file_path.parent_path();
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(VCPKG_LINE_INFO,
                               !ec,
                               "Could not create directory for package file %s",
                               vcpkg::generic_u8string(package_xml_file_path));

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
            return ifw_packages_dir_path /
                   Strings::format("packages.%s.%s", action.spec.name(), action.spec.triplet().canonical_name()) /
                   "data" / "installed";
        }

        void export_unique_packages(const path& raw_exported_dir_path,
                                    std::map<std::string, const ExportPlanAction*> unique_packages,
                                    Filesystem& fs)
        {
            std::error_code ec;

            // packages

            path package_xml_file_path = raw_exported_dir_path / "packages" / "meta" / "package.xml";
            path package_xml_dir_path = package_xml_file_path.parent_path();
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(VCPKG_LINE_INFO,
                               !ec,
                               "Could not create directory for package file %s",
                               vcpkg::generic_u8string(package_xml_file_path));
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

                package_xml_file_path = raw_exported_dir_path / Strings::format("packages.%s", unique_package.first) /
                                        "meta" / "package.xml";
                package_xml_dir_path = package_xml_file_path.parent_path();
                fs.create_directories(package_xml_dir_path, ec);
                Checks::check_exit(VCPKG_LINE_INFO,
                                   !ec,
                                   "Could not create directory for package file %s",
                                   vcpkg::generic_u8string(package_xml_file_path));

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

        void export_unique_triplets(const path& raw_exported_dir_path,
                                    std::set<std::string> unique_triplets,
                                    Filesystem& fs)
        {
            std::error_code ec;

            // triplets

            path package_xml_file_path = raw_exported_dir_path / "triplets" / "meta" / "package.xml";
            path package_xml_dir_path = package_xml_file_path.parent_path();
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(VCPKG_LINE_INFO,
                               !ec,
                               "Could not create directory for package file %s",
                               vcpkg::generic_u8string(package_xml_file_path));
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
                package_xml_file_path =
                    raw_exported_dir_path / Strings::format("triplets.%s", triplet) / "meta" / "package.xml";
                package_xml_dir_path = package_xml_file_path.parent_path();
                fs.create_directories(package_xml_dir_path, ec);
                Checks::check_exit(VCPKG_LINE_INFO,
                                   !ec,
                                   "Could not create directory for package file %s",
                                   vcpkg::generic_u8string(package_xml_file_path));
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

        void export_integration(const path& raw_exported_dir_path, Filesystem& fs)
        {
            std::error_code ec;

            // integration
            path package_xml_file_path = raw_exported_dir_path / "integration" / "meta" / "package.xml";
            path package_xml_dir_path = package_xml_file_path.parent_path();
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(VCPKG_LINE_INFO,
                               !ec,
                               "Could not create directory for package file %s",
                               vcpkg::generic_u8string(package_xml_file_path));

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

            const path config_xml_file_path = get_config_file_path(export_id, ifw_options, paths);

            path config_xml_dir_path = config_xml_file_path.parent_path();
            fs.create_directories(config_xml_dir_path, ec);
            Checks::check_exit(VCPKG_LINE_INFO,
                               !ec,
                               "Could not create directory for configuration file %s",
                               vcpkg::generic_u8string(config_xml_file_path));

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

        void export_maintenance_tool(const path& ifw_packages_dir_path, const VcpkgPaths& paths)
        {
            print2("Exporting maintenance tool...\n");

            std::error_code ec;
            Filesystem& fs = paths.get_filesystem();

            const path& installerbase_exe = paths.get_tool_exe(Tools::IFW_INSTALLER_BASE);
            path tempmaintenancetool = ifw_packages_dir_path / "maintenance" / "data" / "tempmaintenancetool.exe";
            fs.create_directories(tempmaintenancetool.parent_path(), ec);
            Checks::check_exit(VCPKG_LINE_INFO,
                               !ec,
                               "Could not create directory for package file %s",
                               vcpkg::generic_u8string(tempmaintenancetool));
            fs.copy_file(installerbase_exe, tempmaintenancetool, stdfs::copy_options::overwrite_existing, ec);
            Checks::check_exit(
                VCPKG_LINE_INFO, !ec, "Could not write package file %s", vcpkg::generic_u8string(tempmaintenancetool));

            path package_xml_file_path = ifw_packages_dir_path / "maintenance" / "meta" / "package.xml";
            path package_xml_dir_path = package_xml_file_path.parent_path();
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(VCPKG_LINE_INFO,
                               !ec,
                               "Could not create directory for package file %s",
                               vcpkg::generic_u8string(package_xml_file_path));
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
            const path script_source = paths.root / "scripts" / "ifw" / "maintenance.qs";
            const path script_destination = ifw_packages_dir_path / "maintenance" / "meta" / "maintenance.qs";
            fs.copy_file(script_source, script_destination, stdfs::copy_options::overwrite_existing, ec);
            Checks::check_exit(
                VCPKG_LINE_INFO, !ec, "Could not write package file %s", vcpkg::generic_u8string(script_destination));

            print2("Exporting maintenance tool... done\n");
        }

        void do_repository(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            const path& repogen_exe = paths.get_tool_exe(Tools::IFW_REPOGEN);
            const path packages_dir = get_packages_dir_path(export_id, ifw_options, paths);
            const path repository_dir = get_repository_dir_path(export_id, ifw_options, paths);

            print2("Generating repository ", vcpkg::generic_u8string(repository_dir), "...\n");

            std::error_code ec;
            path failure_point;
            Filesystem& fs = paths.get_filesystem();

            fs.remove_all(repository_dir, ec, failure_point);
            Checks::check_exit(VCPKG_LINE_INFO,
                               !ec,
                               "Could not remove outdated repository directory %s due to file %s",
                               generic_u8string(repository_dir),
                               u8string(failure_point));

            auto cmd_line =
                Command(repogen_exe).string_arg("--packages").path_arg(packages_dir).path_arg(repository_dir);

            const int exit_code = cmd_execute_and_capture_output(cmd_line, get_clean_environment()).exit_code;
            Checks::check_exit(VCPKG_LINE_INFO, exit_code == 0, "Error: IFW repository generating failed");

            vcpkg::printf(
                Color::success, "Generating repository %s... done.\n", vcpkg::generic_u8string(repository_dir));
        }

        void do_installer(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
        {
            const path& binarycreator_exe = paths.get_tool_exe(Tools::IFW_BINARYCREATOR);
            const path config_file = get_config_file_path(export_id, ifw_options, paths);
            const path packages_dir = get_packages_dir_path(export_id, ifw_options, paths);
            const path repository_dir = get_repository_dir_path(export_id, ifw_options, paths);
            const path installer_file = get_installer_file_path(export_id, ifw_options, paths);

            vcpkg::printf("Generating installer %s...\n", vcpkg::generic_u8string(installer_file));

            Command cmd_line;

            std::string ifw_repo_url = ifw_options.maybe_repository_url.value_or("");
            if (!ifw_repo_url.empty())
            {
                cmd_line = Command(binarycreator_exe)
                               .string_arg("--online-only")
                               .string_arg("--config")
                               .path_arg(config_file)
                               .string_arg("--repository")
                               .path_arg(repository_dir)
                               .path_arg(installer_file);
            }
            else
            {
                cmd_line = Command(binarycreator_exe)
                               .string_arg("--config")
                               .path_arg(config_file)
                               .string_arg("--packages")
                               .path_arg(packages_dir)
                               .path_arg(installer_file);
            }

            const int exit_code = cmd_execute_and_capture_output(cmd_line, get_clean_environment()).exit_code;
            Checks::check_exit(VCPKG_LINE_INFO, exit_code == 0, "Error: IFW installer generating failed");

            vcpkg::printf(
                Color::success, "Generating installer %s... done.\n", vcpkg::generic_u8string(installer_file));
        }
    }

    void do_export(const std::vector<ExportPlanAction>& export_plan,
                   const std::string& export_id,
                   const Options& ifw_options,
                   const VcpkgPaths& paths)
    {
        std::error_code ec;
        path failure_point;
        Filesystem& fs = paths.get_filesystem();

        // Prepare packages directory
        const path ifw_packages_dir_path = get_packages_dir_path(export_id, ifw_options, paths);

        fs.remove_all(ifw_packages_dir_path, ec, failure_point);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Could not remove outdated packages directory %s due to file %s",
                           generic_u8string(ifw_packages_dir_path),
                           u8string(failure_point));

        fs.create_directory(ifw_packages_dir_path, ec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Could not create packages directory %s",
                           vcpkg::generic_u8string(ifw_packages_dir_path));

        // Export maintenance tool
        export_maintenance_tool(ifw_packages_dir_path, paths);

        vcpkg::printf("Exporting packages %s...\n", vcpkg::generic_u8string(ifw_packages_dir_path));

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
            path ifw_package_dir_path = export_real_package(ifw_packages_dir_path, action, fs);

            // Copy package data
            const InstallDir dirs = InstallDir::from_destination_root(ifw_package_dir_path,
                                                                      action.spec.triplet().to_string(),
                                                                      ifw_package_dir_path / "vcpkg" / "info" /
                                                                          (binary_paragraph.fullstem() + ".list"));

            Install::install_package_and_write_listfile(paths, action.spec, dirs);
        }

        vcpkg::printf("Exporting packages %s... done\n", vcpkg::generic_u8string(ifw_packages_dir_path));

        const path config_file = get_config_file_path(export_id, ifw_options, paths);

        vcpkg::printf("Generating configuration %s...\n", vcpkg::generic_u8string(config_file));

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

        vcpkg::printf("Generating configuration %s... done.\n", vcpkg::generic_u8string(config_file));

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
