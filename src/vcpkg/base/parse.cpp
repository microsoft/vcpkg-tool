#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/util.h>

#include <memory>
#include <utility>

namespace vcpkg
{
    static void advance_rowcol(char32_t ch, int& row, int& column)
    {
        if (row == 0)
        {
            if (column != 0)
            {
                ++column;
            }

            return;
        }

        if (ch == '\t')
        {
            column = ((column + 7) & ~7) + 1; // round to next 8-width tab stop
        }
        else if (ch == '\n')
        {
            row++;
            column = 1;
        }
        else
        {
            ++column;
        }
    }

    void append_caret_line(LocalizedString& res,
                           const Unicode::Utf8Decoder& cursor,
                           const Unicode::Utf8Decoder& start_of_line)
    {
        auto line_end = Util::find_if(cursor, ParserBase::is_lineend);
        StringView line = StringView{
            start_of_line.pointer_to_current(),
            line_end.pointer_to_current(),
        };

        LocalizedString line_prefix = msg::format(msgFormattedParseMessageExpressionPrefix);
        size_t line_prefix_space = 1; // for the space after the prefix
        for (char32_t ch : Unicode::Utf8Decoder(line_prefix))
        {
            line_prefix_space += 1 + Unicode::is_double_width_code_point(ch);
        }

        res.append_indent().append(line_prefix).append_raw(' ').append_raw(line).append_raw('\n');

        std::string caret_string;
        caret_string.append(line_prefix_space, ' ');
        // note *cursor is excluded because it is where the ^ goes
        for (auto it = start_of_line; it != cursor; ++it)
        {
            if (*it == '\t')
                caret_string.push_back('\t');
            else if (Unicode::is_double_width_code_point(*it))
                caret_string.append(2, ' ');
            else
                caret_string.push_back(' ');
        }

        caret_string.push_back('^');

        res.append_indent().append_raw(caret_string);
    }

    static void append_caret_line(LocalizedString& res, const SourceLoc& loc)
    {
        append_caret_line(res, loc.it, loc.start_of_line);
    }

    void ParseMessages::print_errors_or_warnings() const
    {
        for (const auto& line : m_lines)
        {
            line.print_to(out_sink);
        }

        if (!m_good)
        {
            if (m_error_count == 0)
            {
                DiagnosticLine{DiagKind::Error, msg::format(msgWarningsTreatedAsErrors)}.print_to(out_sink);
            }

            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }

    void ParseMessages::exit_if_errors_or_warnings() const
    {
        print_errors_or_warnings();
        if (!m_good)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }

    void ParseMessages::add_line(DiagnosticLine&& line)
    {
        switch (line.kind())
        {
            case DiagKind::Error: ++m_error_count; [[fallthrough]];
            case DiagKind::Warning: m_good = false; break;
            case DiagKind::None:
            case DiagKind::Message:
            case DiagKind::Note: break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        m_lines.push_back(std::move(line));
    }

    LocalizedString ParseMessages::join() const
    {
        std::string combined_messages;
        auto first = m_lines.begin();
        const auto last = m_lines.end();
        if (first != last)
        {
            first->to_string(combined_messages);
            while (++first != last)
            {
                combined_messages.push_back('\n');
                first->to_string(combined_messages);
            }
        }

        return LocalizedString::from_raw(std::move(combined_messages));
    }

    ParserBase::ParserBase(StringView text, Optional<StringView> origin, TextRowCol init_rowcol)
        : m_it(text.begin(), text.end())
        , m_start_of_line(m_it)
        , m_row(init_rowcol.row)
        , m_column(init_rowcol.column)
        , m_text(text)
        , m_origin(origin)
    {
#ifndef NDEBUG
        if (auto check_origin = origin.get())
        {
            if (check_origin->empty())
            {
                Checks::unreachable(VCPKG_LINE_INFO, "origin should not be empty");
            }
        }
#endif
    }

    StringView ParserBase::skip_whitespace() { return match_while(is_whitespace); }
    StringView ParserBase::skip_tabs_spaces()
    {
        return match_while([](char32_t ch) { return ch == ' ' || ch == '\t'; });
    }

    void ParserBase::skip_to_eof() { m_it = m_it.end(); }
    void ParserBase::skip_newline()
    {
        if (cur() == '\r') next();
        if (cur() == '\n') next();
    }
    void ParserBase::skip_line()
    {
        match_until(is_lineend);
        skip_newline();
    }

    bool ParserBase::require_character(char ch)
    {
        if (static_cast<char32_t>(ch) == cur())
        {
            next();
            return false;
        }

        add_error(msg::format(msgExpectedCharacterHere, msg::expected = ch));
        return true;
    }

    bool ParserBase::require_text(StringLiteral text)
    {
        auto encoded = m_it;
        // check that the encoded stream matches the keyword:
        for (const char ch : text)
        {
            if (encoded.is_eof() || *encoded != static_cast<char32_t>(ch))
            {
                add_error(msg::format(msgExpectedTextHere, msg::expected = text));
                return false;
            }

            ++encoded;
        }

        // success
        m_it = encoded;
        if (m_column != 0)
        {
            m_column += static_cast<int>(text.size());
        }

        return true;
    }

    bool ParserBase::try_match_keyword(StringView keyword_content)
    {
        auto encoded = m_it;
        // check that the encoded stream matches the keyword:
        for (const char ch : keyword_content)
        {
            if (encoded.is_eof() || *encoded != static_cast<char32_t>(ch))
            {
                return false;
            }

            ++encoded;
        }

        // whole keyword matched, now check for a word boundary:
        if (!encoded.is_eof() && !is_whitespace(*encoded))
        {
            return false;
        }

        // success
        m_it = encoded;
        if (m_column != 0)
        {
            m_column += static_cast<int>(keyword_content.size());
        }

        return true;
    }

    char32_t ParserBase::next()
    {
        if (m_it == m_it.end())
        {
            return Unicode::end_of_file;
        }
        auto ch = *m_it;
        // See https://www.gnu.org/prep/standards/standards.html#Errors
        advance_rowcol(ch, m_row, m_column);

        ++m_it;
        if (ch == '\n')
        {
            m_start_of_line = m_it;
        }
        if (m_it != m_it.end() && Unicode::utf16_is_surrogate_code_point(*m_it))
        {
            m_it = m_it.end();
        }

        return cur();
    }

    void ParserBase::add_error(LocalizedString&& message) { add_error(std::move(message), cur_loc()); }

    void ParserBase::add_error(LocalizedString&& message, const SourceLoc& loc)
    {
        // avoid cascading errors by only saving the first
        if (!m_messages.any_errors())
        {
            add_line(DiagKind::Error, std::move(message), loc);
        }

        // Avoid error loops by skipping to the end
        skip_to_eof();
    }

    void ParserBase::add_warning(LocalizedString&& message)
    {
        add_line(DiagKind::Warning, std::move(message), cur_loc());
    }

    void ParserBase::add_warning(LocalizedString&& message, const SourceLoc& loc)
    {
        add_line(DiagKind::Warning, std::move(message), loc);
    }

    void ParserBase::add_note(LocalizedString&& message, const SourceLoc& loc)
    {
        add_line(DiagKind::Note, std::move(message), loc);
    }

    void ParserBase::add_line(DiagKind kind, LocalizedString&& message, const SourceLoc& loc)
    {
        message.append_raw('\n');
        append_caret_line(message, loc);
        if (auto origin = m_origin.get())
        {
            m_messages.add_line(DiagnosticLine{kind, *origin, TextRowCol{loc.row, loc.column}, std::move(message)});
        }
        else
        {
            m_messages.add_line(DiagnosticLine{kind, std::move(message)});
        }
    }
}
