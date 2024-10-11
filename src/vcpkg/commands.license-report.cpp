#include <vcpkg/base/files.h>

#include <vcpkg/commands.install.h>
#include <vcpkg/commands.license-report.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/spdx.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace vcpkg
{

    constexpr CommandMetadata CommandLicenseReportMetadata{
        "license-report",
        msgCmdLicenseReportSynopsis,
        {"vcpkg license-report"},
        Undocumented,
        AutocompletePriority::Public,
        0,
        0,
        {},
        nullptr,
    };

    void command_license_report_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        (void)args.parse_arguments(CommandLicenseReportMetadata);

        auto&& fs = paths.get_filesystem();
        auto&& installed_paths = paths.installed();
        LicenseReport report;
        auto status_paragraphs = database_load(fs, installed_paths);
        auto installed_ipvs = get_installed_ports(status_paragraphs);
        if (installed_ipvs.empty())
        {
            msg::println(msgNoInstalledPackagesLicenseReport);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        for (auto&& installed_ipv : installed_ipvs)
        {
            auto spdx_file = installed_paths.spdx_file(installed_ipv.spec());
            auto maybe_spdx_content = fs.try_read_contents(spdx_file);
            if (auto spdx_content = maybe_spdx_content.get())
            {
                auto maybe_parsed_license = read_spdx_license(spdx_content->content, spdx_content->origin);
                if (auto parsed_license = maybe_parsed_license.get())
                {
                    report.named_licenses.insert(*parsed_license);
                    continue;
                }
            }

            report.any_unknown_licenses = true;
        }

        report.print_license_report(msgPackageLicenseSpdx);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
