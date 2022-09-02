#pragma once

#include <vcpkg/base/fwd/parse.h>

#include <vcpkg/base/messages.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/unicode.h>

#include <vcpkg/textrowcol.h>

#include <memory>
#include <string>

namespace vcpkg
{
    struct ParseError
    {
        ParseError(std::string origin, int row, int column, int caret_col, std::string line, std::string message)
            : origin(std::move(origin))
            , row(row)
            , column(column)
            , caret_col(caret_col)
            , line(std::move(line))
            , message(std::move(message))
        {
        }

        const std::string origin;
        const int row;
        const int column;
        const int caret_col;
        const std::string line;
        const std::string message;

        std::string to_string() const;
    };
} // namespace vcpkg

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::ParseError);

namespace vcpkg
{
    inline std::string to_string(const std::unique_ptr<ParseError>& up) { return up->to_string(); }

    struct SourceLoc
    {
        Unicode::Utf8Decoder it;
        Unicode::Utf8Decoder start_of_line;
        int row{};
        int column{};
    };

    struct ParseMessage
    {
        SourceLoc location = {};
        LocalizedString message;

        LocalizedString format(StringView origin, MessageKind kind) const;
    };

    struct ParseMessages
    {
        std::unique_ptr<ParseError> error;
        std::vector<ParseMessage> warnings;

        void exit_if_errors_or_warnings(StringView origin) const;
        bool good() const { return !error && warnings.empty(); }
    };

    struct ParserBase
    {
        ParserBase(StringView text, StringView origin, TextRowCol init_rowcol = {});

        static constexpr bool is_whitespace(char32_t ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }
        static constexpr bool is_lower_alpha(char32_t ch) { return ch >= 'a' && ch <= 'z'; }
        static constexpr bool is_upper_alpha(char32_t ch) { return ch >= 'A' && ch <= 'Z'; }
        static constexpr bool is_icase_alpha(char32_t ch) { return is_lower_alpha(ch) || is_upper_alpha(ch); }
        static constexpr bool is_ascii_digit(char32_t ch) { return ch >= '0' && ch <= '9'; }
        static constexpr bool is_lineend(char32_t ch) { return ch == '\r' || ch == '\n' || ch == Unicode::end_of_file; }
        static constexpr bool is_alphanum(char32_t ch) { return is_icase_alpha(ch) || is_ascii_digit(ch); }
        static constexpr bool is_alphadash(char32_t ch) { return is_icase_alpha(ch) || ch == '-'; }
        static constexpr bool is_alphanumdash(char32_t ch) { return is_alphanum(ch) || ch == '-'; }
        static constexpr bool is_package_name_char(char32_t ch)
        {
            return is_lower_alpha(ch) || is_ascii_digit(ch) || ch == '-';
        }

        static constexpr bool is_hex_digit(char32_t ch)
        {
            return is_ascii_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        }
        static constexpr bool is_word_char(char32_t ch) { return is_alphanum(ch) || ch == '_'; }

        StringView skip_whitespace();
        StringView skip_tabs_spaces();
        void skip_to_eof();
        void skip_newline();
        void skip_line();

        template<class Pred>
        StringView match_while(Pred p)
        {
            const char* start = m_it.pointer_to_current();
            auto ch = cur();
            while (ch != Unicode::end_of_file && p(ch))
            {
                ch = next();
            }

            return {start, m_it.pointer_to_current()};
        }

        template<class Pred>
        StringView match_until(Pred p)
        {
            return match_while([p](char32_t ch) { return !p(ch); });
        }

        bool require_character(char ch);

        bool try_match_keyword(StringView keyword_content);

        StringView text() const { return m_text; }
        Unicode::Utf8Decoder it() const { return m_it; }
        char32_t cur() const { return m_it == m_it.end() ? Unicode::end_of_file : *m_it; }
        SourceLoc cur_loc() const { return {m_it, m_start_of_line, m_row, m_column}; }
        TextRowCol cur_rowcol() const { return {m_row, m_column}; }
        virtual char32_t next();
        bool at_eof() const { return m_it == m_it.end(); }

        void add_error(std::string message) { add_error(std::move(message), cur_loc()); }
        void add_error(std::string message, const SourceLoc& loc);
        void add_error(LocalizedString&& message) { add_error(message.extract_data(), cur_loc()); }
        void add_error(LocalizedString&& message, const SourceLoc& loc) { add_error(message.extract_data(), loc); }

        void add_warning(LocalizedString&& message) { add_warning(std::move(message), cur_loc()); }
        void add_warning(LocalizedString&& message, const SourceLoc& loc);

        const ParseError* get_error() const { return m_messages.error.get(); }
        std::unique_ptr<ParseError> extract_error() { return std::move(m_messages.error); }

        const ParseMessages& messages() const { return m_messages; }
        ParseMessages&& extract_messages() { return std::move(m_messages); }

    private:
        Unicode::Utf8Decoder m_it;
        Unicode::Utf8Decoder m_start_of_line;
        int m_row;
        int m_column;

        StringView m_text;
        StringView m_origin;

        ParseMessages m_messages;
    };
}
