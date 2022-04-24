#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/util.h>

#include <algorithm>
#include <utility>

using namespace vcpkg;

namespace
{
    DECLARE_AND_REGISTER_MESSAGE(WarningsTreatedAsErrors, (), "", "previous warnings being interpreted as errors");

    DECLARE_AND_REGISTER_MESSAGE(FormattedParseMessageExpression,
                                 (msg::value),
                                 "Example of {value} is 'x64 & windows'",
                                 "    on expression: {value}");

    DECLARE_AND_REGISTER_MESSAGE(
        ExpectedCharacterHere,
        (msg::expected),
        "{expected} is a locale-invariant delimiter; for example, the ':' or '=' in 'zlib:x64-windows=skip'",
        "expected '{expected}' here");
}

namespace vcpkg
{
    static void advance_rowcol(char32_t ch, int& row, int& column)
    {
        if (ch == '\t')
            column = (column + 7) / 8 * 8 + 1; // round to next 8-width tab stop
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

    std::string ParseError::format() const
    {
        auto decoder = Unicode::Utf8Decoder(line.data(), line.data() + line.size());
        ParseMessage as_message;
        as_message.location = SourceLoc{std::next(decoder, caret_col), decoder, row, column};
        as_message.message = LocalizedString::from_raw(std::string(message));

        auto res = as_message.format(origin, MessageKind::Error).extract_data();
        res.push_back('\n');
        return res;
    }

    LocalizedString ParseMessage::format(StringView origin, MessageKind kind) const
    {
        LocalizedString res =
            LocalizedString::from_raw(fmt::format("{}:{}:{}: ", origin, location.row, location.column));
        if (kind == MessageKind::Warning)
        {
            res.append(msg::format(msg::msgWarningMessage));
        }
        else
        {
            res.append(msg::format(msg::msgErrorMessage));
        }
        res.append(message);

        res.appendnl();

        auto line_end = Util::find_if(location.it, ParserBase::is_lineend);
        StringView line = StringView{
            location.start_of_line.pointer_to_current(),
            line_end.pointer_to_current(),
        };
        res.append(msg::format(msgFormattedParseMessageExpression, msg::value = line));
        res.appendnl();

        auto caret_point = StringView{location.start_of_line.pointer_to_current(), location.it.pointer_to_current()};
        auto formatted_caret_point = msg::format(msgFormattedParseMessageExpression, msg::value = caret_point);

        std::string caret_string;
        caret_string.reserve(formatted_caret_point.data().size());
        for (char32_t ch : Unicode::Utf8Decoder(formatted_caret_point))
        {
            if (ch == '\t')
                caret_string.push_back('\t');
            else if (Unicode::is_double_width_code_point(ch))
                caret_string.append("  ");
            else
                caret_string.push_back(' ');
        }
        caret_string.push_back('^');

        res.append_raw(std::move(caret_string));

        return res;
    }

    const std::string& ParseError::get_message() const { return this->message; }

    void ParseMessages::exit_if_errors_or_warnings(StringView origin) const
    {
        for (const auto& warning : warnings)
        {
            msg::println(warning.format(origin, MessageKind::Warning));
        }

        if (error)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, LocalizedString::from_raw(error->format()));
        }

        if (!warnings.empty())
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgWarningsTreatedAsErrors);
        }
    }

    ParserBase::ParserBase(StringView text, StringView origin, TextRowCol init_rowcol)
        : m_it(text.begin(), text.end())
        , m_start_of_line(m_it)
        , m_row(init_rowcol.row_or(1))
        , m_column(init_rowcol.column_or(1))
        , m_text(text)
        , m_origin(origin)
    {
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

    void ParserBase::add_warning(LocalizedString&& message, const SourceLoc& loc)
    {
        m_messages.warnings.push_back(ParseMessage{loc, std::move(message)});
    }

    void ParserBase::add_error(std::string message, const SourceLoc& loc)
    {
        // avoid cascading errors by only saving the first
        if (!m_messages.error)
        {
            // find end of line
            auto line_end = loc.it;
            while (line_end != line_end.end() && *line_end != '\n' && *line_end != '\r')
            {
                ++line_end;
            }
            m_messages.error = std::make_unique<ParseError>(
                m_origin.to_string(),
                loc.row,
                loc.column,
                static_cast<int>(std::distance(loc.start_of_line, loc.it)),
                std::string(loc.start_of_line.pointer_to_current(), line_end.pointer_to_current()),
                std::move(message));
        }

        // Avoid error loops by skipping to the end
        skip_to_eof();
    }
}
