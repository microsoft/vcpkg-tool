#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/util.h>

#include <memory>
#include <utility>

namespace vcpkg
{
    static void advance_rowcol(char32_t ch, int& row, int& column)
    {
        if (ch == '\t')
            column = ((column + 7) & ~7) + 1; // round to next 8-width tab stop
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

    static void append_caret_line(LocalizedString& res, const SourceLoc& loc)
    {
        auto line_end = Util::find_if(loc.it, ParserBase::is_lineend);
        StringView line = StringView{
            loc.start_of_line.pointer_to_current(),
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
        // note *it is excluded because it is where the ^ goes
        for (auto it = loc.start_of_line; it != loc.it; ++it)
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

    LocalizedString ParseMessage::format(StringView origin, MessageKind kind) const
    {
        LocalizedString res;
        if (!origin.empty())
        {
            res.append_raw(fmt::format("{}:{}:{}: ", origin, location.row, location.column));
        }

        res.append_raw(kind == MessageKind::Warning ? WarningPrefix : ErrorPrefix);
        res.append(message);
        res.append_raw('\n');
        append_caret_line(res, location);
        return res;
    }

    ParserBase::ParserBase(DiagnosticContext& context, StringView text, Optional<StringView> origin, int init_row)
        : m_it(text.begin(), text.end())
        , m_start_of_line(m_it)
        , m_row(init_row)
        , m_column(init_row == 0 ? 0 : 1)
        , m_text(text)
        , m_origin(origin)
        , m_context(context)
        , m_any_errors(false)
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

    ParserBase ParserBase::clone_with_context(DiagnosticContext& context)
    {
        return ParserBase(m_it, m_start_of_line, m_row, m_column, m_text, m_origin, context, m_any_errors);
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
        m_column += static_cast<int>(keyword_content.size());
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
        if (!m_any_errors)
        {
            message.append_raw('\n');
            append_caret_line(message, loc);
            if (auto origin = m_origin.get())
            {
                m_context.report(
                    DiagnosticLine{DiagKind::Error, *origin, TextRowCol{loc.row, loc.column}, std::move(message)});
            }
            else
            {
                m_context.report(DiagnosticLine{DiagKind::Error, std::move(message)});
            }

            m_any_errors = true;
            // Avoid error loops by skipping to the end
            skip_to_eof();
        }
    }

    void ParserBase::add_warning(LocalizedString&& message) { add_warning(std::move(message), cur_loc()); }

    void ParserBase::add_warning(LocalizedString&& message, const SourceLoc& loc)
    {
        message.append_raw('\n');
        append_caret_line(message, loc);
        if (auto origin = m_origin.get())
        {
            m_context.report(
                DiagnosticLine{DiagKind::Warning, *origin, TextRowCol{loc.row, loc.column}, std::move(message)});
        }
        else
        {
            m_context.report(DiagnosticLine{DiagKind::Warning, std::move(message)});
        }
    }

    ParserBase::ParserBase(Unicode::Utf8Decoder it,
                           Unicode::Utf8Decoder start_of_line,
                           int row,
                           int column,
                           StringView text,
                           Optional<StringView> origin,
                           DiagnosticContext& context,
                           bool any_errors)
        : m_it(it)
        , m_start_of_line(start_of_line)
        , m_row(row)
        , m_column(column)
        , m_text(text)
        , m_origin(origin)
        , m_context(context)
        , m_any_errors(any_errors)
    {
    }
}
