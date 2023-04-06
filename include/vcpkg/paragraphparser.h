#pragma once

#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/fwd/paragraphparser.h>

#include <vcpkg/base/expected.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/textrowcol.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcpkg
{
    struct ParseControlErrorInfo
    {
        std::string name;
        std::vector<std::string> missing_fields;
        std::vector<std::string> extra_fields;
        std::map<StringLiteral, LocalizedString, std::less<>> expected_types;
        std::vector<LocalizedString> other_errors;
        LocalizedString error;

        bool has_error() const
        {
            return !missing_fields.empty() || !extra_fields.empty() || !expected_types.empty() ||
                   !other_errors.empty() || !error.empty();
        }

        static std::string format_errors(View<std::unique_ptr<ParseControlErrorInfo>> errors);
        void to_string(std::string& target) const;
        std::string to_string() const;
    };
} // namespace vcpkg

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::ParseControlErrorInfo);

namespace vcpkg
{
    inline std::string to_string(const std::unique_ptr<ParseControlErrorInfo>& up) { return up->to_string(); }
    template<class P>
    using ParseExpected = vcpkg::ExpectedT<std::unique_ptr<P>, std::unique_ptr<ParseControlErrorInfo>>;

    using Paragraph = std::map<std::string, std::pair<std::string, TextRowCol>, std::less<>>;

    struct ParagraphParser
    {
        ParagraphParser(Paragraph&& fields) : fields(std::move(fields)) { }

        std::string required_field(StringLiteral fieldname);
        void required_field(StringLiteral fieldname, std::string& out);
        void required_field(StringLiteral fieldname, std::pair<std::string&, TextRowCol&> out);

        std::string optional_field(StringLiteral fieldname);
        void optional_field(StringLiteral fieldname, std::pair<std::string&, TextRowCol&> out);

        void add_type_error(StringLiteral fieldname, const LocalizedString& type) { expected_types[fieldname] = type; }

        std::unique_ptr<ParseControlErrorInfo> error_info(StringView name) const;

    private:
        Paragraph&& fields;
        std::vector<std::string> missing_fields;
        std::map<StringLiteral, LocalizedString, std::less<>> expected_types;
    };

    ExpectedL<std::vector<std::string>> parse_default_features_list(const std::string& str,
                                                                    StringView origin = "<unknown>",
                                                                    TextRowCol textrowcol = {});
    ExpectedL<std::vector<ParsedQualifiedSpecifier>> parse_qualified_specifier_list(const std::string& str,
                                                                                    StringView origin = "<unknown>",
                                                                                    TextRowCol textrowcol = {});
}
