#pragma once

#include <vcpkg/base/fwd/parse.h>

#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/unicode.h>

#include <string>

namespace vcpkg
{
    void append_caret_line(LocalizedString& res,
                           const Unicode::Utf8Decoder& it,
                           const Unicode::Utf8Decoder& start_of_line);

    struct ParseMessages
    {
        void print_errors_or_warnings() const;
        void exit_if_errors_or_warnings() const;
        bool good() const noexcept { return m_good; }
        bool any_errors() const noexcept { return m_error_count != 0; }
        size_t error_count() const noexcept { return m_error_count; }

        const std::vector<DiagnosticLine>& lines() const noexcept { return m_lines; }

        void add_line(DiagnosticLine&& line);

        LocalizedString join() const;

    private:
        std::vector<DiagnosticLine> m_lines;
        bool m_good = true;
        size_t m_error_count = 0;
    };

    struct ParserBase
    {
        ParserBase(StringView text, Optional<StringView> origin, TextRowCol init_rowcol);

        static constexpr bool is_whitespace(char32_t ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }
        static constexpr bool is_lower_alpha(char32_t ch) { return ch >= 'a' && ch <= 'z'; }
        static constexpr bool is_lower_digit(char ch) { return is_lower_alpha(ch) || is_ascii_digit(ch); }
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
        static constexpr bool is_hex_digit_lower(char32_t ch) { return is_ascii_digit(ch) || (ch >= 'a' && ch <= 'f'); }
        static constexpr bool is_hex_digit(char32_t ch) { return is_hex_digit_lower(ch) || (ch >= 'A' && ch <= 'F'); }
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
        bool require_text(StringLiteral keyword);

        bool try_match_keyword(StringView keyword_content);

        StringView text() const { return m_text; }
        Unicode::Utf8Decoder it() const { return m_it; }
        char32_t cur() const { return m_it == m_it.end() ? Unicode::end_of_file : *m_it; }
        SourceLoc cur_loc() const { return {m_it, m_start_of_line, m_row, m_column}; }
        TextRowCol cur_rowcol() const { return {m_row, m_column}; }
        char32_t next();
        bool at_eof() const { return m_it == m_it.end(); }

        void add_error(LocalizedString&& message);
        void add_error(LocalizedString&& message, const SourceLoc& loc);

        void add_warning(LocalizedString&& message);
        void add_warning(LocalizedString&& message, const SourceLoc& loc);

        void add_note(LocalizedString&& message, const SourceLoc& loc);

        const ParseMessages& messages() const { return m_messages; }
        ParseMessages&& extract_messages() { return std::move(m_messages); }

    private:
        void add_line(DiagKind kind, LocalizedString&& message, const SourceLoc& loc);

        Unicode::Utf8Decoder m_it;
        Unicode::Utf8Decoder m_start_of_line;
        int m_row;
        int m_column;

        StringView m_text;
        Optional<StringView> m_origin;

        ParseMessages m_messages;
    };
}
