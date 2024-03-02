
#include <vcpkg/base/checks.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/commands.help.h>
#include <vcpkg/input.h>
#include <vcpkg/packagespec.h>

#include <utility>

namespace vcpkg
{
    PackageSpec parse_package_spec(StringView spec_string, Triplet default_triplet, const LocalizedString& example_text)
    {
        auto maybe_qualified_specifier = parse_qualified_specifier(Strings::ascii_to_lowercase(spec_string));
        if (auto qualified_specifier = maybe_qualified_specifier.get())
        {
            auto expected_spec = qualified_specifier->to_package_spec(default_triplet);
            if (auto spec = expected_spec.get())
            {
                return std::move(*spec);
            }

            msg::println(Color::error, expected_spec.error());
        }
        else
        {
            msg::println(Color::error, maybe_qualified_specifier.error());
        }

        msg::println(Color::none, example_text);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    void check_triplet(StringView name, const TripletDatabase& database)
    {
        // Intentionally show the lowercased string
        auto as_lower = Strings::ascii_to_lowercase(name);
        if (!database.is_valid_triplet_canonical_name(as_lower))
        {
            msg::println_error(msgInvalidTriplet, msg::triplet = as_lower);
            help_topic_valid_triplet(database);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }

    PackageSpec check_and_get_package_spec(StringView spec_string,
                                           Triplet default_triplet,
                                           const LocalizedString& example_text,
                                           const TripletDatabase& database)
    {
        auto maybe_qualified_specifier = parse_qualified_specifier(Strings::ascii_to_lowercase(spec_string));
        if (auto qualified_specifier = maybe_qualified_specifier.get())
        {
            if (auto specified_triplet = qualified_specifier->triplet.get())
            {
                check_triplet(*specified_triplet, database);
            }

            auto expected_spec = qualified_specifier->to_package_spec(default_triplet);
            if (auto spec = expected_spec.get())
            {
                return std::move(*spec);
            }

            msg::println(Color::error, expected_spec.error());
        }
        else
        {
            msg::println(Color::error, maybe_qualified_specifier.error());
        }

        msg::println(Color::none, example_text);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    FullPackageSpec check_and_get_full_package_spec(StringView spec_string,
                                                    Triplet default_triplet,
                                                    const LocalizedString& example_text,
                                                    const TripletDatabase& database)
    {
        auto maybe_qualified_specifier = parse_qualified_specifier(Strings::ascii_to_lowercase(spec_string));
        if (auto qualified_specifier = maybe_qualified_specifier.get())
        {
            if (auto specified_triplet = qualified_specifier->triplet.get())
            {
                check_triplet(*specified_triplet, database);
            }

            auto expected_spec = qualified_specifier->to_full_spec(default_triplet, ImplicitDefault::YES);
            if (auto spec = expected_spec.get())
            {
                return std::move(*spec);
            }

            msg::println(Color::error, expected_spec.error());
        }
        else
        {
            msg::println(Color::error, maybe_qualified_specifier.error());
        }

        msg::println(Color::none, example_text);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
}
