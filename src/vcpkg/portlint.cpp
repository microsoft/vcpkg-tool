#include <vcpkg/base/messages.h>
#include <vcpkg/base/util.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/portlint.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/versions.h>

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

    namespace
    {
        std::string::size_type find_param(const std::string& content,
                                          ZStringView param,
                                          std::string::size_type start,
                                          std::string::size_type end)
        {
            while ((start = content.find(param.c_str(), start)) != std::string::npos)
            {
                if (start > end) return std::string::npos;
                // param must be not included in another word
                if (ParserBase::is_word_char(content[start - 1]) ||
                    ParserBase::is_word_char(content[start + param.size()]))
                {
                    start += param.size();
                }
                else
                {
                    return start;
                }
            }
            return std::string::npos;
        }

        void replace_param(std::string& content,
                           ZStringView old_param,
                           ZStringView new_param,
                           std::string::size_type start,
                           std::string::size_type end)
        {
            auto index = find_param(content, old_param, start, end);
            if (index != std::string::npos)
            {
                content.replace(index, old_param.size(), new_param.c_str());
            }
        }

        void remove_param(std::string& content, ZStringView param, std::string::size_type index)
        {
            const auto first_white_space = content.find_last_not_of(" \n\t\r", index - 1) + 1;
            const auto white_space_length = index - first_white_space;
            content.erase(first_white_space, white_space_length + param.size());
        }

        void find_remove_param(std::string& content,
                               ZStringView param,
                               std::string::size_type start,
                               std::string::size_type end)
        {
            const auto start_param = find_param(content, param, start, end);
            if (start_param != std::string::npos)
            {
                remove_param(content, param, start_param);
            }
        }

        struct ParamAndValue
        {
            std::string::size_type start_param = std::string::npos;
            std::string::size_type start_value = std::string::npos;
            std::string::size_type end_value = std::string::npos;
            bool found() const { return start_param != std::string::npos; }
            bool value_found() const { return start_value != std::string::npos && end_value != std::string::npos; }
            std::string get_value(const std::string& content) const
            {
                return content.substr(start_value, end_value - start_value);
            }
            std::string::size_type full_length() const { return end_value - start_param; }
        };

        ParamAndValue find_param_maybe_value(const std::string& content,
                                             ZStringView param,
                                             std::string::size_type start,
                                             std::string::size_type end)
        {
            auto start_param = find_param(content, param, start, end);
            if (start_param == std::string::npos) return {};
            const auto start_value = content.find_first_not_of(" \r\n\t)", start_param + param.size());
            const auto end_value = content.find_first_of(" \r\n\t)", start_value);
            if (end_value == std::string::npos || end_value > end) return ParamAndValue{start_param};
            return ParamAndValue{start_param, start_value, end_value};
        }
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
                status |= Status::Fixed;
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
            find_remove_param(portfile_content, "PREFER_NINJA", index, end);
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
            auto target = find_param_maybe_value(portfile_content, "TARGET_PATH", index, end);
            if (target.found())
            {
                if (target.value_found())
                {
                    const auto original_param = target.get_value(portfile_content);
                    auto param = StringView(original_param);
                    if (Strings::starts_with(param, "share/"))
                    {
                        param = param.substr(StringLiteral("share/").size());
                    }
                    if (param == "${PORT}" || Strings::case_insensitive_ascii_equals(param, origin))
                    {
                        portfile_content.erase(target.start_param, target.full_length());
                    }
                    else
                    {
                        portfile_content.replace(
                            target.start_param, target.full_length() - param.size(), "PACKAGE_NAME ");
                    }
                    // remove the CONFIG_PATH part if it uses the same param
                    auto config = find_param_maybe_value(portfile_content, "CONFIG_PATH", index, end);
                    if (config.value_found())
                    {
                        if (config.get_value(portfile_content) == original_param)
                        {
                            const auto start_next = portfile_content.find_first_not_of(' ', config.end_value);
                            portfile_content.erase(config.start_param, start_next - config.start_param);
                        }
                    }
                }
                else
                {
                    remove_param(portfile_content, "TARGET_PATH", target.start_param);
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
            auto out_source_path = find_param_maybe_value(portfile_content, "OUT_SOURCE_PATH", index, end);
            if (out_source_path.value_found())
            {
                const auto param_value = out_source_path.get_value(portfile_content);

                const auto before_param = portfile_content.find_last_not_of(' ', out_source_path.start_param - 1) + 1;
                auto after_value = portfile_content.find_first_not_of(" \r", out_source_path.end_value);
                const std::string line_ending = portfile_content[after_value - 1] == '\r' ? "\r\n" : "\n";
                if (portfile_content[after_value] != '\n')
                {
                    // if OUT_SOURCE_PATH is not on its own line, don't remove new line character
                    after_value -= line_ending.size();
                }
                // remove '   OUT_SOURCE_PATH FOOBAR  ' line
                portfile_content.erase(before_param, after_value + 1 - before_param);

                // Replace 'REF' by 'SOURCE_BASE'
                replace_param(portfile_content, "REF", "SOURCE_BASE", index, end);

                // insert 'FOOBAR' or // '\n   FOOBAR' after '('
                auto open_bracket = portfile_content.find('(', index);
                if (open_bracket != std::string::npos && open_bracket < out_source_path.start_param)
                {
                    char c = portfile_content[before_param - 1];
                    if (c == '\n')
                    {
                        // if the OUT_SOURCE_PATH was in a new line, insert the param in a new line
                        auto num_spaces = out_source_path.start_param - before_param;
                        portfile_content.insert(open_bracket + 1,
                                                line_ending + std::string(num_spaces, ' ') + param_value);
                    }
                    else
                    {
                        portfile_content.insert(open_bracket + 1, param_value + ' ');
                    }
                }
            }
            portfile_content.replace(
                index, StringLiteral("vcpkg_extract_source_archive_ex").size(), "vcpkg_extract_source_archive");
        }
        index = 0;
        while ((index = portfile_content.find("vcpkg_check_features", index)) != std::string::npos)
        {
            const auto end = portfile_content.find(')', index);
            const auto features = find_param(portfile_content, "FEATURES", index, end);
            const auto inverted_features = find_param(portfile_content, "INVERTED_FEATURES", index, end);
            if (features == std::string::npos && inverted_features == std::string::npos)
            {
                if (fix == Fix::NO)
                {
                    status |= Status::Problem;
                    warningsSink.println_warning(msgLintVcpkgCheckFeatures, msg::package_name = origin);
                    break;
                }
                status |= Status::Fixed;

                auto feature_options = find_param_maybe_value(portfile_content, "OUT_FEATURE_OPTIONS", index, end);
                if (feature_options.value_found())
                {
                    portfile_content.insert(feature_options.end_value, " FEATURES");
                }
            }
            index = end;
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
