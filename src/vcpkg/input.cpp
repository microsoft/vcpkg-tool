#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg
{
    PackageSpec Input::check_and_get_package_spec(std::string&& spec_string,
                                                  Triplet default_triplet,
                                                  CStringView example_text)
    {
        const std::string as_lowercase = Strings::ascii_to_lowercase(std::move(spec_string));
        auto expected_spec = FullPackageSpec::from_string(as_lowercase, default_triplet);
        if (const auto spec = expected_spec.get())
        {
            return PackageSpec{spec->package_spec};
        }

        // Intentionally show the lowercased string
        print2(Color::error, expected_spec.error());
        print2(example_text);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    void Input::check_triplet(Triplet t, const VcpkgPaths& paths)
    {
        if (!paths.is_valid_triplet(t))
        {
            print2(Color::error, "Error: invalid triplet: ", t, '\n');
            Help::help_topic_valid_triplet(paths);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }

    FullPackageSpec Input::check_and_get_full_package_spec(std::string&& full_package_spec_as_string,
                                                           Triplet default_triplet,
                                                           CStringView example_text)
    {
        const std::string as_lowercase = Strings::ascii_to_lowercase(std::move(full_package_spec_as_string));
        auto expected_spec = FullPackageSpec::from_string(as_lowercase, default_triplet);
        if (const auto spec = expected_spec.get())
        {
            return *spec;
        }

        // Intentionally show the lowercased string
        print2(Color::error, expected_spec.error());
        print2(example_text);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
}
