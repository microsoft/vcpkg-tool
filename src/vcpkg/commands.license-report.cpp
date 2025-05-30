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
                auto maybe_license_text = read_spdx_license_text(spdx_content->content, spdx_content->origin);
                if (auto license_text = maybe_license_text.get())
                {
                    ParseMessages pm;
                    auto parsed = parse_spdx_license_expression(*license_text, pm);
                    pm.print_errors_or_warnings();
                    if (pm.any_errors())
                    {
                        report.any_unknown_licenses = true;
                    }

                    for (auto&& applicable_license : parsed.applicable_licenses())
                    {
                        if (applicable_license.license_text == SpdxLicenseRefVcpkgNull)
                        {
                            report.any_unknown_licenses = true;
                        }
                        else
                        {
                            report.named_licenses.insert(applicable_license.to_string());
                        }
                    }

                    continue;
                }
            }

            report.any_unknown_licenses = true;
        }

        report.print_license_report(msgPackageLicenseSpdx);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
