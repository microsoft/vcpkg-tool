#include <vcpkg/base/messages.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/portlint.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/versions.h>

#include "vcpkg/base/system.print.h"
#include "vcpkg/base/util.h"

namespace vcpkg::Lint
{

    constexpr StringLiteral VERSION_RELAXED = "version";
    constexpr StringLiteral VERSION_DATE = "version-date";
    constexpr StringLiteral VERSION_STRING = "version-string";

    VersionScheme get_recommended_version_scheme(StringView raw_version, VersionScheme original_scheme)
    {
        if (DateVersion::try_parse(raw_version)) return VersionScheme::Date;
        if (DotVersion::try_parse_relaxed(raw_version)) return VersionScheme::Relaxed;
        return original_scheme;
    }

    Status check_used_version_scheme(SourceControlFile& scf, Fix fix)
    {
        auto scheme = scf.core_paragraph->version_scheme;
        if (scheme == VersionScheme::String)
        {
            auto new_scheme = get_recommended_version_scheme(scf.core_paragraph->raw_version, scheme);
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

    namespace
    {
        std::string::size_type find_license(const std::string& license_expression,
                                            StringView license_identifier,
                                            std::string::size_type offset = 0)
        {
            std::string::size_type index;
            while ((index = license_expression.find(license_identifier.data(), offset, license_identifier.size())) !=
                   std::string::npos)
            {
                const auto end_index = index + license_identifier.size();
                if (end_index >= license_expression.size() || license_expression[end_index] == ' ')
                {
                    return index;
                }
                offset = end_index;
            }
            return std::string::npos;
        }
    }

    std::string get_recommended_license_expression(std::string original_license)
    {
        static constexpr std::pair<StringLiteral, StringLiteral> deprecated_licenses[] = {
            {"AGPL-1.0", "AGPL-1.0-only"},
            {"AGPL-3.0", "AGPL-3.0-only"},
            {"GFDL-1.1", "GFDL-1.1-or-later"},
            {"GFDL-1.2", "GFDL-1.2-or-later"},
            {"GFDL-1.3", "GFDL-1.3-or-later"},
            {"GPL-1.0", "GPL-1.0-only"},
            {"GPL-1.0+", "GPL-1.0-or-later"},
            {"GPL-2.0", "GPL-2.0-only"},
            {"GPL-2.0+", "GPL-2.0-or-later"},
            {"GPL-2.0-with-autoconf-exception", "GPL-2.0-only WITH Autoconf-exception-2.0"},
            {"GPL-2.0-with-bison-exception", "GPL-2.0-or-later WITH Bison-exception-2.2"},
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
            {"StandardML-NJ", "SMLNJ"}};
        for (const auto dep_license : deprecated_licenses)
        {
            std::string::size_type index = 0;
            while ((index = find_license(original_license, dep_license.first, index)) != std::string::npos)
            {
                original_license.replace(index, dep_license.first.size(), dep_license.second.c_str());
            }
        }
        return original_license;
    }

    Status check_license_expression(SourceControlFile& scf, Fix fix)
    {
        if (!scf.core_paragraph->license.has_value())
        {
            msg::println_warning(msgLintMissingLicenseExpression, msg::package_name = scf.core_paragraph->name);
            return Status::Problem;
        }

        Status status = Status::Ok;
        auto& license = scf.core_paragraph->license.value_or_exit(VCPKG_LINE_INFO);
        auto new_expr = get_recommended_license_expression(license);
        if (new_expr != license)
        {
            if (fix == Fix::YES)
            {
                license = new_expr;
            }
            else
            {
                msg::println_warning(msgLintDeprecatedLicenseExpressionWithReplacement,
                                     msg::package_name = scf.core_paragraph->name,
                                     msg::actual = license,
                                     msg::new_value = new_expr);
                status |= Status::Problem;
            }
        }
        static constexpr std::pair<StringLiteral, StringLiteral> deprecated_licenses_WITH[] = {
            {"eCos-2.0", "eCos-exception-2.0"},
            {"GPL-2.0-with-classpath-exception", "Classpath-exception-2.0"},
            {"GPL-2.0-with-font-exception", "Font-exception-2.0"},
            {"wxWindows", "WxWindows-exception-3.1"},
        };
        for (const auto dep_license : deprecated_licenses_WITH)
        {
            if (find_license(license, dep_license.first) != std::string::npos)
            {
                msg::println_warning(msgLintDeprecatedLicenseExpressionWithoutReplacement,
                                     msg::package_name = scf.core_paragraph->name,
                                     msg::actual = dep_license.first,
                                     msg::new_value = dep_license.second);
                status |= Status::Problem;
            }
        }
        return status;
    }

    Status check_portfile_deprecated_functions(Filesystem& fs, SourceControlFileAndLocation& scf, Fix fix)
    {
        auto contents = fs.read_contents(scf.source_location / "portfile.cmake", VCPKG_LINE_INFO);
        auto lint_result = check_portfile_deprecated_functions(
            std::move(contents), scf.source_control_file->core_paragraph->name, fix, stdout_sink);

        if (lint_result.status == Status::Fixed || lint_result.status == Status::PartiallyFixed)
        {
            fs.write_contents(
                scf.source_location / "portfile.cmake", lint_result.new_portfile_content, VCPKG_LINE_INFO);

            for (StringView name : lint_result.added_host_deps)
            {
                if (!Util::any_of(scf.source_control_file->core_paragraph->dependencies,
                                  [&](const Dependency& d) { return d.name == name; }))
                {
                    scf.source_control_file->core_paragraph->dependencies.push_back(
                        Dependency{name.to_string(), {}, {}, {}, true});
                }
            }
        }
        return lint_result.status;
    }

    FixedPortfile check_portfile_deprecated_functions(std::string&& portfile_content,
                                                      StringView origin,
                                                      Fix fix,
                                                      MessageSink& warningsSink)
    {
        FixedPortfile fixedPortfile;
        Status status = Status::Ok;
        const auto handle_warning = [&](StringLiteral deprecated, StringLiteral new_func, bool can_fix = true) {
            if (fix == Fix::NO || !can_fix)
            {
                status |= Status::Problem;
                warningsSink.println_warning(msgLintDeprecatedFunction,
                                             msg::package_name = origin,
                                             msg::actual = deprecated,
                                             msg::expected = new_func);
            }
            else
            {
                status = Status::Fixed;
            }
        };
        if (Strings::contains(portfile_content, "vcpkg_build_msbuild"))
        {
            handle_warning("vcpkg_build_msbuild", "vcpkg_install_msbuild", false);
        }
        std::string::size_type index = 0;
        while ((index = portfile_content.find("vcpkg_configure_cmake", index)) != std::string::npos)
        {
            handle_warning("vcpkg_configure_cmake", "vcpkg_cmake_configure");
            if (fix == Fix::NO)
            {
                break;
            }
            const auto end = portfile_content.find(')', index);
            const auto ninja = portfile_content.find("PREFER_NINJA", index);
            if (ninja != std::string::npos && ninja < end)
            {
                const auto start = portfile_content.find_last_not_of(" \n\t\r", ninja - 1) + 1;
                portfile_content.erase(start, (ninja - start) + StringLiteral("PREFER_NINJA").size());
            }
            portfile_content.replace(index, StringLiteral("vcpkg_configure_cmake").size(), "vcpkg_cmake_configure");
            fixedPortfile.added_host_deps.insert("vcpkg-cmake");
        }
        if (Strings::contains(portfile_content, "vcpkg_build_cmake"))
        {
            handle_warning("vcpkg_build_cmake", "vcpkg_cmake_build");
            if (fix == Fix::YES)
            {
                Strings::inplace_replace_all(portfile_content, "vcpkg_build_cmake", "vcpkg_cmake_build");
                fixedPortfile.added_host_deps.insert("vcpkg-cmake");
            }
        }
        if (Strings::contains(portfile_content, "vcpkg_install_cmake"))
        {
            handle_warning("vcpkg_install_cmake", "vcpkg_cmake_install");
            if (fix == Fix::YES)
            {
                Strings::inplace_replace_all(portfile_content, "vcpkg_install_cmake", "vcpkg_cmake_install");
                fixedPortfile.added_host_deps.insert("vcpkg-cmake");
            }
        }
        index = 0;
        while ((index = portfile_content.find("vcpkg_fixup_cmake_targets", index)) != std::string::npos)
        {
            handle_warning("vcpkg_fixup_cmake_targets", "vcpkg_fixup_cmake_targets");
            if (fix == Fix::NO)
            {
                break;
            }
            const auto end = portfile_content.find(')', index);
            const auto target = portfile_content.find("TARGET_PATH");
            if (target != std::string::npos && target < end)
            {
                auto start_param = target + StringLiteral("TARGET_PATH").size();
                start_param = portfile_content.find_first_not_of(" \n\t)", start_param);
                const auto end_param = portfile_content.find_first_of(" \n\t)", start_param);
                if (end_param != std::string::npos && end_param <= end)
                {
                    const auto original_param = portfile_content.substr(start_param, end_param - start_param);
                    auto param = StringView(original_param);
                    if (Strings::starts_with(param, "share/"))
                    {
                        param = param.substr(StringLiteral("share/").size());
                    }
                    if (param == "${PORT}" || Strings::case_insensitive_ascii_equals(param, origin))
                    {
                        portfile_content.erase(target, end_param - target);
                    }
                    else
                    {
                        portfile_content.replace(target, (end_param - target) - param.size(), "PACKAGE_NAME ");
                    }
                    // remove the CONFIG_PATH part if it uses the same param
                    const auto start_config_path = portfile_content.find("CONFIG_PATH", index);
                    if (start_config_path != std::string::npos && start_config_path < end)
                    {
                        start_param = start_config_path + StringLiteral("CONFIG_PATH").size();
                        start_param = portfile_content.find_first_not_of(" \n\t)", start_param);
                        const auto end_config_param = portfile_content.find_first_of(" \n\t)", start_param);
                        const auto config_param =
                            StringView(portfile_content).substr(start_param, end_config_param - start_param);
                        if (config_param == original_param)
                        {
                            const auto start_next = portfile_content.find_first_not_of(' ', end_config_param);
                            portfile_content.erase(start_config_path, start_next - start_config_path);
                        }
                    }
                }
                else
                {
                    const auto start = portfile_content.find_last_not_of(" \n\t\r", target - 1) + 1;
                    portfile_content.erase(start, StringLiteral("TARGET_PATH").size() + (target - start));
                }
            }
            portfile_content.replace(
                index, StringLiteral("vcpkg_fixup_cmake_targets").size(), "vcpkg_cmake_config_fixup");
            fixedPortfile.added_host_deps.insert("vcpkg-cmake-config");
        }
        index = 0;
        while ((index = portfile_content.find("vcpkg_extract_source_archive_ex", index)) != std::string::npos)
        {
            handle_warning("vcpkg_extract_source_archive_ex", "vcpkg_extract_source_archive");
            if (fix == Fix::NO)
            {
                break;
            }
            const auto end = portfile_content.find(')', index);
            const auto target = portfile_content.find("OUT_SOURCE_PATH");
            if (target != std::string::npos && target < end)
            {
                const auto before_out_source_path = portfile_content.find_last_not_of(' ', target - 1);
                auto start_param = target + StringLiteral("OUT_SOURCE_PATH").size();
                start_param = portfile_content.find_first_not_of(" \n\r\t)", start_param);
                const auto end_param = portfile_content.find_first_of(" \n\r\t)", start_param);
                if (end_param != std::string::npos && end_param <= end)
                {
                    const auto out_source_path_param = portfile_content.substr(start_param, end_param - start_param);
                    const auto after_param = portfile_content.find_first_not_of(' ', end_param);
                    auto erase_lenth = after_param - before_out_source_path;
                    bool was_crlf = false;
                    if (portfile_content[after_param] != '\n' && portfile_content[after_param] != '\r')
                    {
                        erase_lenth -= 1; // if OUT_SOURCE_PATH is not on its own line, don't remove new line character
                    }
                    if (portfile_content[after_param] == '\r')
                    {
                        erase_lenth += 1; // \r\n is used as line ending
                        was_crlf = true;
                    }
                    // remove '   OUT_SOURCE_PATH FOOBAR  ' line
                    portfile_content.erase(before_out_source_path + 1, erase_lenth);
                    // insert 'FOOBAR' or // '\n   FOOBAR' after '('
                    auto open_bracket = portfile_content.find('(', index);
                    if (open_bracket != std::string::npos && open_bracket < end)
                    {
                        char c = portfile_content[before_out_source_path];
                        if (c == '\n')
                        {
                            // if the OUT_SOURCE_PATH was in a new line, insert the param in a new line
                            portfile_content.insert(open_bracket + 1,
                                                    (was_crlf ? "\r\n" : "\n") +
                                                        std::string(target - before_out_source_path - 1, ' ') +
                                                        out_source_path_param);
                        }
                        else
                        {
                            portfile_content.insert(open_bracket + 1, out_source_path_param + ' ');
                        }
                    }
                }
            }
            portfile_content.replace(
                index, StringLiteral("vcpkg_extract_source_archive_ex").size(), "vcpkg_extract_source_archive");
        }
        fixedPortfile.status = status;
        if (fix == Fix::YES)
        {
            fixedPortfile.new_portfile_content = std::move(portfile_content);
        }
        return fixedPortfile;
    }

    Status& operator|=(Status& self, Status s)
    {
        self = static_cast<Status>(static_cast<std::underlying_type_t<Status>>(self) |
                                   static_cast<std::underlying_type_t<Status>>(s));
        return self;
    }

}
