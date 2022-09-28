#include <vcpkg/base/messages.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/portlint.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/versions.h>

#include "vcpkg/base/system.print.h"
#include "vcpkg/base/system.process.h"
#include "vcpkg/base/util.h"
#include "vcpkg/installedpaths.h"
#include "vcpkg/vcpkgpaths.h"

namespace vcpkg::Lint
{

    constexpr StringLiteral VERSION_RELAXED = "version";
    constexpr StringLiteral VERSION_SEMVER = "version-semver";
    constexpr StringLiteral VERSION_DATE = "version-date";
    constexpr StringLiteral VERSION_STRING = "version-string";

    Status check_used_version_scheme(SourceControlFile& scf, Fix fix)
    {
        auto scheme = scf.core_paragraph->version_scheme;
        auto new_scheme = scheme;
        if (scheme == VersionScheme::String)
        {
            if (DateVersion::try_parse(scf.core_paragraph->raw_version))
            {
                new_scheme = VersionScheme::Date;
            }
            else if (DotVersion::try_parse_relaxed(scf.core_paragraph->raw_version))
            {
                new_scheme = VersionScheme::Relaxed;
            }
            if (scheme != new_scheme)
            {
                if (fix == Fix::YES)
                {
                    scf.core_paragraph->version_scheme = new_scheme;
                    return Status::Fixed;
                }
                msg::println_warning(msgLintSuggestNewVersionScheme,
                                     msg::new_scheme =
                                         (new_scheme == VersionScheme::Date) ? VERSION_DATE : VERSION_RELAXED,
                                     msg::old_scheme = VERSION_STRING,
                                     msg::package_name = scf.core_paragraph->name);
                return Status::Problem;
            }
        }
        return Status::Ok;
    }

    Status check_license_expression(SourceControlFile& scf, Fix fix)
    {
        if (!scf.core_paragraph->license.has_value())
        {
            msg::println_warning(msgLintMissingLicenseExpression, msg::package_name = scf.core_paragraph->name);
            return Status::Problem;
        }
        const std::pair<StringLiteral, StringLiteral> deprecated_licenses[] = {
            {"AGPL-1.0", "AGPL-1.0-only"},
            {"AGPL-3.0", "AGPL-3.0-only"},
            {"eCos-2.0",
             "DEPRECATED: Use license expression including main license, \"WITH\" operator, and identifier: "
             "eCos-exception-2.0"},
            {"GFDL-1.1", "GFDL-1.1-or-later"},
            {"GFDL-1.2", "GFDL-1.2-or-later"},
            {"GFDL-1.3", "GFDL-1.3-or-later"},
            {"GPL-1.0", "GPL-1.0-only"},
            {"GPL-1.0+", "GPL-1.0-or-later"},
            {"GPL-2.0", "GPL-2.0-only"},
            {"GPL-2.0+", "GPL-2.0-or-later"},
            {"GPL-2.0-with-autoconf-exception", "GPL-2.0-only WITH Autoconf-exception-2.0"},
            {"GPL-2.0-with-bison-exception", "GPL-2.0-or-later WITH Bison-exception-2.2"},
            {"GPL-2.0-with-classpath-exception",
             "DEPRECATED: Use license expression including main license, \"WITH\" operator, and identifier: "
             "Classpath-exception-2.0"},
            {"GPL-2.0-with-font-exception",
             "DEPRECATED: Use license expression including main license, \"WITH\" operator, and identifier: "
             "Font-exception-2.0"},
            {"GPL-2.0-with-GCC-exception", "GPL-2.0-or-later WITH GCC-exception-2.0"},
            {"GPL-3.0", "GPL-3.0-only"},
            {"GPL-3.0+", "GPL-3.0-or-later"},
            {"GPL-3.0-with-autoconf-exception", "GPL-3.0-only WITH Autoconf-exception-3.0"},
            {"GPL-3.0-with-GCC-exception", "GPL-3.0-only WITH GCC-exception-3.1"},
            {"LGPL-2.0", "LGPL-2.0-only"},
            {"LGPL-2.0+", "LGPL-2.0-or-later"},
            {"LGPL-2.1", "LGPL-2.1-only"},
            {"LGPL-2.1+", "LGPL-2.1-or-later"},
            {"LGPL-3.0", "LGPL-3.0-only"},
            {"LGPL-3.0+", "LGPL-3.0-or-later"},
            {"Nunit",
             "DEPRECATED: This license is based on the MIT license, except with an \"acknowledgement\" clause. That "
             "clause makes it functionally equivalent to MIT with advertising (Free, but GPLv2/v3 incompatible)"},
            {"StandardML-NJ", "SMLNJ"},
            {"wxWindows",
             "DEPRECATED: Use license expression including main license, \"WITH\" operator, and identifier: "
             "WxWindows-exception-3.1"}};
        Status status = Status::Ok;
        auto& license = scf.core_paragraph->license.value_or_exit(VCPKG_LINE_INFO);
        for (const auto dep_license : deprecated_licenses)
        {
            const auto index = license.find(dep_license.first.c_str());
            if (index == std::string::npos)
            {
                continue;
            }
            const auto end_index = index + dep_license.first.size();
            if (end_index < license.size() && license[end_index] != ' ')
            {
                continue;
            }
            if (Strings::starts_with(dep_license.second, "DEPRECATED"))
            {
                msg::println_warning(msg::format(msgLintDeprecatedLicenseExpressionWithoutReplacement,
                                                 msg::package_name = scf.core_paragraph->name,
                                                 msg::actual = dep_license.first)
                                         .append_raw(dep_license.second.substr(StringLiteral("DEPRECATED:").size())));

                status |= Status::Problem;
            }
            else if (fix == Fix::NO)
            {
                msg::println_warning(msgLintDeprecatedLicenseExpressionWithReplacement,
                                     msg::package_name = scf.core_paragraph->name,
                                     msg::actual = dep_license.first,
                                     msg::new_value = dep_license.second);
                status |= Status::Problem;
            }
            else
            {
                license.replace(index, dep_license.first.size(), dep_license.second.c_str());
                status |= Status::Fixed;
            }
        }

        return status;
    }

    Status check_portfile_deprecated_functions(Filesystem& fs, SourceControlFileAndLocation& scf, Fix fix)
    {
        Status status = Status::Ok;
        auto content = fs.read_contents(scf.source_location / "portfile.cmake", VCPKG_LINE_INFO);
        const auto handle_warning = [&](StringLiteral deprecated, StringLiteral new_func, bool can_fix = true) {
            if (fix == Fix::NO || !can_fix)
            {
                status |= Status::Problem;
                msg::println_warning(msgLintDeprecatedFunction,
                                     msg::package_name = scf.source_control_file->core_paragraph->name,
                                     msg::actual = deprecated,
                                     msg::expected = new_func);
            }
            else
            {
                status = Status::Fixed;
            }
        };
        const auto add_host_dep = [&](std::string name) {
            if (!Util::any_of(scf.source_control_file->core_paragraph->dependencies,
                              [&](const Dependency& d) { return d.name == name; }))
            {
                scf.source_control_file->core_paragraph->dependencies.push_back(Dependency{name, {}, {}, {}, true});
            }
        };
        if (Strings::contains(content, "vcpkg_build_msbuild"))
        {
            handle_warning("vcpkg_build_msbuild", "vcpkg_install_msbuild", false);
        }
        std::string::size_type index = 0;
        while ((index = content.find("vcpkg_configure_cmake", index)) != std::string::npos)
        {
            handle_warning("vcpkg_configure_cmake", "vcpkg_cmake_configure");
            if (fix == Fix::NO)
            {
                break;
            }
            const auto end = content.find(')', index);
            const auto ninja = content.find("PREFER_NINJA", index);
            if (ninja != std::string::npos && ninja < end)
            {
                const auto start = content.find_last_not_of(" \n\t\r", ninja - 1) + 1;
                content.erase(start, (ninja - start) + StringLiteral("PREFER_NINJA").size());
            }
            content.replace(index, StringLiteral("vcpkg_configure_cmake").size(), "vcpkg_cmake_configure");
            add_host_dep("vcpkg-cmake");
        }
        if (Strings::contains(content, "vcpkg_build_cmake"))
        {
            handle_warning("vcpkg_build_cmake", "vcpkg_cmake_build");
            Strings::inplace_replace_all(content, "vcpkg_build_cmake", "vcpkg_cmake_build");
            add_host_dep("vcpkg-cmake");
        }
        if (Strings::contains(content, "vcpkg_install_cmake"))
        {
            handle_warning("vcpkg_install_cmake", "vcpkg_cmake_install");
            Strings::inplace_replace_all(content, "vcpkg_install_cmake", "vcpkg_cmake_install");
        }
        index = 0;
        while ((index = content.find("vcpkg_fixup_cmake_targets", index)) != std::string::npos)
        {
            handle_warning("vcpkg_fixup_cmake_targets", "vcpkg_fixup_cmake_targets");
            if (fix == Fix::NO)
            {
                break;
            }
            const auto end = content.find(')', index);
            const auto target = content.find("TARGET_PATH");
            if (target != std::string::npos && target < end)
            {
                auto start_param = target + StringLiteral("TARGET_PATH").size();
                start_param = content.find_first_not_of(" \n\t)", start_param);
                const auto end_param = content.find_first_of(" \n\t)", start_param);
                if (end_param != std::string::npos && end_param <= end)
                {
                    const auto original_param = content.substr(start_param, end_param - start_param);
                    auto param = StringView(original_param);
                    if (Strings::starts_with(param, "share/"))
                    {
                        param = param.substr(StringLiteral("share/").size());
                    }
                    if (param == "${PORT}" ||
                        Strings::case_insensitive_ascii_equals(param, scf.source_control_file->core_paragraph->name))
                    {
                        content.erase(target, end_param - target);
                    }
                    else
                    {
                        content.replace(target, (end_param - target) - param.size(), "PACKAGE_NAME ");
                    }
                    // remove the CONFIG_PATH part if it uses the same param
                    const auto start_config_path = content.find("CONFIG_PATH", index);
                    if (start_config_path != std::string::npos && start_config_path < end)
                    {
                        auto start_param = start_config_path + StringLiteral("CONFIG_PATH").size();
                        start_param = content.find_first_not_of(" \n\t)", start_param);
                        const auto end_param = content.find_first_of(" \n\t)", start_param);
                        const auto config_param = StringView(content).substr(start_param, end_param - start_param);
                        if (config_param == original_param)
                        {
                            const auto start_next = content.find_first_not_of(' ', end_param);
                            content.erase(start_config_path, start_next - start_config_path);
                        }
                    }
                }
                else
                {
                    const auto start = content.find_last_not_of(" \n\t\r", target - 1) + 1;
                    content.erase(start, StringLiteral("TARGET_PATH").size() + (target - start));
                }
            }
            content.replace(index, StringLiteral("vcpkg_fixup_cmake_targets").size(), "vcpkg_cmake_config_fixup");
            add_host_dep("vcpkg-cmake-config");
        }
        if (status == Status::Fixed || status == Status::PartiallyFixed)
        {
            fs.write_contents(scf.source_location / "portfile.cmake", content, VCPKG_LINE_INFO);
        }
        return status;
    }

    Status& operator|=(Status& self, Status s)
    {
        self = static_cast<Status>(static_cast<std::underlying_type_t<Status>>(self) |
                                   static_cast<std::underlying_type_t<Status>>(s));
        return self;
    }
}
