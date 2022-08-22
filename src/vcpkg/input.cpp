#include <vcpkg/base/system.print.h>

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
                                           ZStringView example_text,
                                           const VcpkgPaths& paths)
    {
        const std::string as_lowercase = Strings::ascii_to_lowercase(std::move(spec_string));

        auto expected_spec =
            parse_qualified_specifier(as_lowercase).then(&ParsedQualifiedSpecifier::to_package_spec, default_triplet);
        if (auto spec = expected_spec.get())
        {
            check_triplet(spec->triplet(), paths);
            return std::move(*spec);
        }

        // Intentionally show the lowercased string
        print2(Color::error, expected_spec.error());
        print2(example_text);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    void check_triplet(Triplet t, const VcpkgPaths& paths)
    {
        if (!paths.is_valid_triplet(t))
        {
            print2(Color::error, "Error: invalid triplet: ", t, '\n');
            LockGuardPtr<Metrics>(g_metrics)->track_property("error", "invalid triplet: " + t.to_string());
            Help::help_topic_valid_triplet(paths);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }

    FullPackageSpec check_and_get_full_package_spec(std::string&& full_package_spec_as_string,
                                                    Triplet default_triplet,
                                                    ZStringView example_text,
                                                    const VcpkgPaths& paths)
    {
        const std::string as_lowercase = Strings::ascii_to_lowercase(std::move(full_package_spec_as_string));
        auto expected_spec = parse_qualified_specifier(as_lowercase)
                                 .then(&ParsedQualifiedSpecifier::to_full_spec, default_triplet, ImplicitDefault::YES);
        if (const auto spec = expected_spec.get())
        {
            check_triplet(spec->package_spec.triplet(), paths);
            return *spec;
        }

        // Intentionally show the lowercased string
        print2(Color::error, expected_spec.error());
        print2(example_text);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
}
