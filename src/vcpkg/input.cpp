#include <vcpkg/base/checks.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/commands.help.h>
#include <vcpkg/input.h>
#include <vcpkg/packagespec.h>

#include <utility>

namespace vcpkg
{
    [[nodiscard]] ExpectedL<PackageSpec> parse_package_spec(StringView spec_string, Triplet default_triplet)
    {
        return parse_qualified_specifier(Strings::ascii_to_lowercase(spec_string),
                                         AllowFeatures::No,
                                         ParseExplicitTriplet::Allow,
                                         AllowPlatformSpec::No)
            .map([&](ParsedQualifiedSpecifier&& qualified_specifier) -> PackageSpec {
                return qualified_specifier.to_package_spec(default_triplet);
            });
    }

    [[nodiscard]] ExpectedL<Unit> check_triplet(StringView name, const TripletDatabase& database)
    {
        Unicode::Utf8Decoder start_of_line{name};
        for (auto cursor = start_of_line; !cursor.is_eof(); ++cursor)
        {
            if (!ParserBase::is_package_name_char(*cursor))
            {
                auto result = msg::format_error(msgParseTripletNotEof).append_raw('\n');
                append_caret_line(result, cursor, start_of_line);
                result.append_raw('\n');
                append_help_topic_valid_triplet(result, database);
                return result;
            }
        }

        if (!database.is_valid_triplet_canonical_name(name))
        {
            LocalizedString result = msg::format_error(msgInvalidTriplet, msg::triplet = name);
            result.append_raw('\n');
            append_help_topic_valid_triplet(result, database);
            return result;
        }

        return Unit{};
    }

    [[nodiscard]] ExpectedL<PackageSpec> check_and_get_package_spec(StringView spec_string,
                                                                    Triplet default_triplet,
                                                                    const TripletDatabase& database)
    {
        return parse_qualified_specifier(Strings::ascii_to_lowercase(spec_string),
                                         AllowFeatures::No,
                                         ParseExplicitTriplet::Allow,
                                         AllowPlatformSpec::No)
            .then([&](ParsedQualifiedSpecifier&& qualified_specifier) -> ExpectedL<PackageSpec> {
                if (auto specified_triplet = qualified_specifier.triplet.get())
                {
                    auto maybe_error = check_triplet(specified_triplet->value, database);
                    if (!maybe_error.has_value())
                    {
                        return std::move(maybe_error).error();
                    }
                }

                return qualified_specifier.to_package_spec(default_triplet);
            });
    }

    [[nodiscard]] ExpectedL<FullPackageSpec> check_and_get_full_package_spec(StringView spec_string,
                                                                             Triplet default_triplet,
                                                                             const TripletDatabase& database)
    {
        return parse_qualified_specifier(Strings::ascii_to_lowercase(spec_string),
                                         AllowFeatures::Yes,
                                         ParseExplicitTriplet::Allow,
                                         AllowPlatformSpec::No)
            .then([&](ParsedQualifiedSpecifier&& qualified_specifier) -> ExpectedL<FullPackageSpec> {
                if (auto specified_triplet = qualified_specifier.triplet.get())
                {
                    auto maybe_error = check_triplet(specified_triplet->value, database);
                    if (!maybe_error.has_value())
                    {
                        return std::move(maybe_error).error();
                    }
                }

                return qualified_specifier.to_full_spec(default_triplet, ImplicitDefault::Yes);
            });
    }
}
