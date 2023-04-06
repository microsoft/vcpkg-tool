#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/commands.h>
#include <vcpkg/export.chocolatey.h>
#include <vcpkg/export.h>
#include <vcpkg/install.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Export::Chocolatey
{
    static std::string create_nuspec_dependencies(const BinaryParagraph& binary_paragraph,
                                                  const std::map<PackageSpec, std::string>& packages_version)
    {
        static constexpr auto CONTENT_TEMPLATE = R"(<dependency id="@PACKAGE_ID@" version="[@PACKAGE_VERSION@]" />)";

        std::string nuspec_dependencies;
        for (const auto& depend : binary_paragraph.dependencies)
        {
            auto found = packages_version.find(depend);
            if (found == packages_version.end())
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgFailedToObtainDependencyVersion);
            }
            std::string nuspec_dependency = Strings::replace_all(CONTENT_TEMPLATE, "@PACKAGE_ID@", depend.name());
            Strings::inplace_replace_all(nuspec_dependency, "@PACKAGE_VERSION@", found->second);
            nuspec_dependencies += nuspec_dependency;
        }
        return nuspec_dependencies;
    }

    static std::string create_nuspec_file_contents(StringView exported_root_dir,
                                                   const BinaryParagraph& binary_paragraph,
                                                   const std::map<PackageSpec, std::string>& packages_version,
                                                   const Options& chocolatey_options)
    {
        static constexpr auto CONTENT_TEMPLATE = R"(<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2010/07/nuspec.xsd">
    <metadata>
        <id>@PACKAGE_ID@</id>
        <version>@PACKAGE_VERSION@</version>
        <authors>@PACKAGE_MAINTAINER@</authors>
        <description><![CDATA[
            @PACKAGE_DESCRIPTION@
        ]]></description>
        <dependencies>
            @PACKAGE_DEPENDENCIES@
        </dependencies>
    </metadata>
    <files>
        <file src="@EXPORTED_ROOT_DIR@\installed\**" target="installed" />
        <file src="@EXPORTED_ROOT_DIR@\tools\**" target="tools" />
    </files>
</package>
)";
        auto package_version = packages_version.find(binary_paragraph.spec);
        if (package_version == packages_version.end())
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgFailedToObtainPackageVersion);
        }

        std::string nuspec_file_content =
            Strings::replace_all(CONTENT_TEMPLATE, "@PACKAGE_ID@", binary_paragraph.spec.name());
        Strings::inplace_replace_all(nuspec_file_content, "@PACKAGE_VERSION@", package_version->second);
        Strings::inplace_replace_all(
            nuspec_file_content, "@PACKAGE_MAINTAINER@", chocolatey_options.maybe_maintainer.value_or(""));
        Strings::inplace_replace_all(
            nuspec_file_content, "@PACKAGE_DESCRIPTION@", Strings::join("\n", binary_paragraph.description));
        Strings::inplace_replace_all(nuspec_file_content, "@EXPORTED_ROOT_DIR@", exported_root_dir);
        Strings::inplace_replace_all(nuspec_file_content,
                                     "@PACKAGE_DEPENDENCIES@",
                                     create_nuspec_dependencies(binary_paragraph, packages_version));
        return nuspec_file_content;
    }

    static std::string create_chocolatey_install_contents()
    {
        static constexpr auto CONTENT_TEMPLATE = R"###(
$ErrorActionPreference = 'Stop';

$packageName= $env:ChocolateyPackageName
$toolsDir   = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$rootDir    = "$(Split-Path -parent $toolsDir)"
$installedDir = Join-Path $rootDir 'installed'

$whereToInstall = (pwd).path
$whereToInstallCache = Join-Path $rootDir 'install.txt'
Set-Content -Path $whereToInstallCache -Value $whereToInstall
Copy-Item $installedDir -destination $whereToInstall -recurse -force
)###";
        return CONTENT_TEMPLATE;
    }

    static std::string create_chocolatey_uninstall_contents(const BinaryParagraph& binary_paragraph)
    {
        static constexpr auto CONTENT_TEMPLATE = R"###(
$ErrorActionPreference = 'Stop';

$packageName= $env:ChocolateyPackageName
$toolsDir   = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$rootDir    = "$(Split-Path -parent $toolsDir)"
$listFile = Join-Path $rootDir 'installed\vcpkg\info\@PACKAGE_FULLSTEM@.list'

$whereToInstall = $null
$whereToInstallCache = Join-Path $rootDir 'install.txt'
Get-Content $whereToInstallCache | Foreach-Object {
    $whereToInstall = $_
}

$installedDir = Join-Path $whereToInstall 'installed'
Get-Content $listFile | Foreach-Object {
    $fileToRemove = Join-Path $installedDir $_
    if (Test-Path $fileToRemove -PathType Leaf) {
        Remove-Item $fileToRemove
    }
}

Get-Content $listFile | Foreach-Object {
    $fileToRemove = Join-Path $installedDir $_
    if (Test-Path $fileToRemove -PathType Container) {
        $folderToDelete = Join-Path $fileToRemove *
        if (-Not (Test-Path $folderToDelete))
        {
            Remove-Item $fileToRemove
        }
    }
}

$listFileToRemove = Join-Path $whereToInstall 'installed\vcpkg\info\@PACKAGE_FULLSTEM@.list'
Remove-Item $listFileToRemove

if (Test-Path $installedDir)
{
    while (
        $empties = Get-ChildItem $installedDir -recurse -Directory | Where-Object {
            $_.GetFiles().Count -eq 0 -and $_.GetDirectories().Count -eq 0
        }
    ) { $empties | Remove-Item }
}
)###";
        std::string chocolatey_uninstall_content =
            Strings::replace_all(CONTENT_TEMPLATE, "@PACKAGE_FULLSTEM@", binary_paragraph.fullstem());
        return chocolatey_uninstall_content;
    }

    void do_export(const std::vector<ExportPlanAction>& export_plan,
                   const VcpkgPaths& paths,
                   const Options& chocolatey_options)
    {
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               chocolatey_options.maybe_maintainer.has_value(),
                               msgOptionRequired,
                               msg::option = "x-maintainer");

        Filesystem& fs = paths.get_filesystem();
        const auto vcpkg_root_path = paths.root;
        const auto raw_exported_dir_path = vcpkg_root_path / "chocolatey";
        const auto exported_dir_path = vcpkg_root_path / "chocolatey_exports";
        const Path& nuget_exe = paths.get_tool_exe(Tools::NUGET, stdout_sink);

        fs.remove_all(raw_exported_dir_path, VCPKG_LINE_INFO);
        fs.create_directory(raw_exported_dir_path, VCPKG_LINE_INFO);
        fs.remove_all(exported_dir_path, VCPKG_LINE_INFO);
        fs.create_directory(exported_dir_path, VCPKG_LINE_INFO);

        // execute the plan
        std::map<PackageSpec, std::string> packages_version;
        for (const ExportPlanAction& action : export_plan)
        {
            if (action.plan_type != ExportPlanType::ALREADY_BUILT)
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }

            const BinaryParagraph& binary_paragraph = action.core_paragraph().value_or_exit(VCPKG_LINE_INFO);
            auto norm_version = binary_paragraph.version;

            // normalize the version string to be separated by dots to be compliant with Nuspec.
            Strings::inplace_replace_all(norm_version, '-', '.');
            Strings::inplace_replace_all(norm_version, '_', '.');
            norm_version += chocolatey_options.maybe_version_suffix.value_or("");
            packages_version.emplace(binary_paragraph.spec, norm_version);
        }

        for (const ExportPlanAction& action : export_plan)
        {
            const std::string display_name = action.spec.to_string();
            msg::println(msgExportingPackage, msg::package_name = display_name);

            const auto per_package_dir_path = raw_exported_dir_path / action.spec.name();

            const BinaryParagraph& binary_paragraph = action.core_paragraph().value_or_exit(VCPKG_LINE_INFO);

            const InstalledPaths installed(per_package_dir_path / "installed");

            const InstallDir dirs =
                InstallDir::from_destination_root(installed, action.spec.triplet(), binary_paragraph);

            install_package_and_write_listfile(fs, paths.package_dir(action.spec), dirs);

            const std::string nuspec_file_content = create_nuspec_file_contents(
                per_package_dir_path, binary_paragraph, packages_version, chocolatey_options);
            const auto nuspec_file_path =
                per_package_dir_path / Strings::concat(binary_paragraph.spec.name(), ".nuspec");
            fs.write_contents(nuspec_file_path, nuspec_file_content, VCPKG_LINE_INFO);

            fs.create_directory(per_package_dir_path / "tools", IgnoreErrors{});

            const std::string chocolatey_install_content = create_chocolatey_install_contents();
            const auto chocolatey_install_file_path = per_package_dir_path / "tools" / "chocolateyInstall.ps1";
            fs.write_contents(chocolatey_install_file_path, chocolatey_install_content, VCPKG_LINE_INFO);

            const std::string chocolatey_uninstall_content = create_chocolatey_uninstall_contents(binary_paragraph);
            const auto chocolatey_uninstall_file_path = per_package_dir_path / "tools" / "chocolateyUninstall.ps1";
            fs.write_contents(chocolatey_uninstall_file_path, chocolatey_uninstall_content, VCPKG_LINE_INFO);

            auto cmd_line = Command(nuget_exe)
                                .string_arg("pack")
                                .string_arg("-OutputDirectory")
                                .string_arg(exported_dir_path)
                                .string_arg(nuspec_file_path)
                                .string_arg("-NoDefaultExcludes");

            flatten(cmd_execute_and_capture_output(cmd_line, default_working_directory, get_clean_environment()),
                    Tools::NUGET)
                .value_or_exit(VCPKG_LINE_INFO);
        }
    }
}
