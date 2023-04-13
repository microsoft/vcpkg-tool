#include <vcpkg/base/strings.h>

#include <vcpkg/commands.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/metrics.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg
{
    PackageSpec check_and_get_package_spec(std::string&& spec_string,
                                           Triplet default_triplet,
                                           bool& default_triplet_used,
                                           const LocalizedString& example_text,
                                           const VcpkgPaths& paths)
    {
        const std::string as_lowercase = Strings::ascii_to_lowercase(std::move(spec_string));

        auto expected_spec =
            parse_qualified_specifier(as_lowercase)
                .then(&ParsedQualifiedSpecifier::to_package_spec, default_triplet, default_triplet_used);
        if (auto spec = expected_spec.get())
        {
            check_triplet(spec->triplet(), paths);
            return std::move(*spec);
        }

        // Intentionally show the lowercased string
        msg::println(Color::error, expected_spec.error());
        msg::println(Color::none, example_text);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    void check_triplet(Triplet t, const VcpkgPaths& paths)
    {
        if (!paths.is_valid_triplet(t))
        {
            msg::println_error(msgInvalidTriplet, msg::triplet = t);
            Help::help_topic_valid_triplet(paths);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }

    FullPackageSpec check_and_get_full_package_spec(std::string full_package_spec_as_string,
                                                    Triplet default_triplet,
                                                    bool& default_triplet_used,
                                                    const LocalizedString& example_text,
                                                    const VcpkgPaths& paths)
    {
        Strings::ascii_to_lowercase(full_package_spec_as_string);
        auto expected_spec = parse_qualified_specifier(full_package_spec_as_string)
                                 .then(&ParsedQualifiedSpecifier::to_full_spec,
                                       default_triplet,
                                       default_triplet_used,
                                       ImplicitDefault::YES);
        if (const auto spec = expected_spec.get())
        {
            check_triplet(spec->package_spec.triplet(), paths);
            return *spec;
        }

        // Intentionally show the lowercased string
        msg::println(Color::error, expected_spec.error());
        msg::println(Color::none, example_text);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
}
