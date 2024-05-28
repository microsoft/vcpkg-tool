#pragma once

#include <vcpkg/fwd/paragraphparser.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/textrowcol.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vcpkg
{
    using Paragraph = std::map<std::string, std::pair<std::string, TextRowCol>, std::less<>>;

    using FieldValue = std::pair<std::string, TextRowCol>;

    struct ParagraphParser
    {
        ParagraphParser(StringView origin, Paragraph&& fields)
            : origin(origin.data(), origin.size()), fields(std::move(fields))
        {
        }

        std::string required_field(StringLiteral fieldname);

        Optional<FieldValue> optional_field(StringLiteral fieldname);
        std::string optional_field_or_empty(StringLiteral fieldname);

        void add_error(TextRowCol position, msg::MessageT<> error_content);

        Optional<LocalizedString> error() const;

    private:
        std::string origin;
        Paragraph&& fields;
        std::vector<LocalizedString> errors;
    };

    ExpectedL<std::vector<std::string>> parse_default_features_list(const std::string& str,
                                                                    Optional<StringView> origin,
                                                                    TextRowCol textrowcol);
    ExpectedL<std::vector<ParsedQualifiedSpecifier>> parse_qualified_specifier_list(const std::string& str,
                                                                                    Optional<StringView> origin,
                                                                                    TextRowCol textrowcol);
}
