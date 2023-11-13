#pragma once

#include <vcpkg/fwd/paragraphparser.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/textrowcol.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vcpkg
{
    struct ParseControlErrorInfo
    {
        std::string name;
        std::vector<std::string> extra_fields;
        std::vector<LocalizedString> other_errors;

        bool has_error() const
        {
            return !extra_fields.empty() ||
                   !other_errors.empty();
        }

        static std::string format_errors(View<std::unique_ptr<ParseControlErrorInfo>> errors);
        void to_string(std::string& target) const;
        std::string to_string() const;

        static std::unique_ptr<ParseControlErrorInfo> from_error(StringView port_name, LocalizedString&& ls);
    };
} // namespace vcpkg

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::ParseControlErrorInfo);

namespace vcpkg
{
    inline std::string to_string(const std::unique_ptr<ParseControlErrorInfo>& up) { return up->to_string(); }
    template<class P>
    using ParseExpected = vcpkg::ExpectedT<std::unique_ptr<P>, std::unique_ptr<ParseControlErrorInfo>>;

    static constexpr struct ToLocalizedString_t
    {
        LocalizedString operator()(std::unique_ptr<ParseControlErrorInfo> p) const;
    } ToLocalizedString;

    using Paragraph = std::map<std::string, std::pair<std::string, TextRowCol>, std::less<>>;

    struct ParagraphParser
    {
        ParagraphParser(StringView origin, Paragraph&& fields) : origin(origin.data(), origin.size()), fields(std::move(fields)) { }

        std::string required_field(StringLiteral fieldname);

        std::string optional_field(StringLiteral fieldname);
        std::string optional_field(StringLiteral fieldname, TextRowCol& position);

        void add_error(TextRowCol position, msg::MessageT<> error_content);

        std::unique_ptr<ParseControlErrorInfo> error_info() const;

    private:
        std::string origin;
        Paragraph&& fields;
        std::vector<LocalizedString> other_errors;
    };

    ExpectedL<std::vector<std::string>> parse_default_features_list(const std::string& str,
                                                                    StringView origin = "<unknown>",
                                                                    TextRowCol textrowcol = {});
    ExpectedL<std::vector<ParsedQualifiedSpecifier>> parse_qualified_specifier_list(const std::string& str,
                                                                                    StringView origin = "<unknown>",
                                                                                    TextRowCol textrowcol = {});
}
