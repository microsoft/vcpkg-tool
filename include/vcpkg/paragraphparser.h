#pragma once

#include <vcpkg/fwd/paragraphparser.h>

#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/packagespec.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vcpkg
{
    using Paragraph = std::map<std::string, std::pair<std::string, TextPosition>, std::less<>>;

    struct ParagraphParser
    {
        ParagraphParser(StringView origin, Paragraph&& fields)
            : origin(origin.data(), origin.size()), fields(std::move(fields))
        {
        }

        std::string required_field(StringLiteral fieldname);

        std::string optional_field(StringLiteral fieldname);
        std::string optional_field(StringLiteral fieldname, TextPosition& position);

        void add_error(TextPosition position, msg::MessageT<> error_content);

        Optional<LocalizedString> error() const;

    private:
        std::string origin;
        Paragraph&& fields;
        std::vector<LocalizedString> errors;
    };

    Optional<std::vector<std::string>> parse_default_features_list_context(DiagnosticContext& context,
                                                                           const std::string& str,
                                                                           StringView origin = "<unknown>",
                                                                           TextPosition position = {1, 1});
    ExpectedL<std::vector<std::string>> parse_default_features_list(const std::string& str,
                                                                    StringView origin = "<unknown>",
                                                                    TextPosition position = {1, 1});
    Optional<std::vector<ParsedQualifiedSpecifier>> parse_qualified_specifier_list(DiagnosticContext& context,
                                                                                   const std::string& str,
                                                                                   StringView origin = "<unknown>",
                                                                                   TextPosition position = {1, 1});
    ExpectedL<std::vector<ParsedQualifiedSpecifier>> parse_qualified_specifier_list(const std::string& str,
                                                                                    StringView origin = "<unknown>",
                                                                                    TextPosition position = {1, 1});
}
