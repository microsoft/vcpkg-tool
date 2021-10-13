#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <utility>

using namespace vcpkg;

namespace vcpkg::Parse
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

    namespace
    {
        DECLARE_AND_REGISTER_MESSAGE(ParseErrorFormat, (msg::file, msg::row, msg::column, msg::error, msg::line, msg::spaces),
        "The {spaces} _must_ line up with {line} in the translation; add or remove spaces to line 3 so that this works.",
        R"({file}:{row}:{column}: error: {error}\n
    on expression: {line}
                   {spaces}^
)");
    }

    msg::LocalizedString ParseError::format() const
    {
        std::string caret_spacing;
        auto decoder = Unicode::Utf8Decoder(line.data(), line.data() + line.size());
        for (int i = 0; i < caret_col; ++i, ++decoder)
        {
            const char32_t cp = *decoder;
            // this may eventually want to check for full-width characters and grapheme clusters as well
            caret_spacing.push_back(cp == '\t' ? '\t' : ' ');
        }

        return msg::format(msgParseErrorFormat,
            msg::file = origin,
            msg::row = row,
            msg::column = column,
            msg::error = message,
            msg::line = line,
            msg::spaces = caret_spacing);
    }

    const msg::LocalizedString& ParseError::get_message() const { return this->message; }

    ParserBase::ParserBase(StringView text, StringView origin, TextRowCol init_rowcol)
        : m_it(text.begin(), text.end())
        , m_start_of_line(m_it)
        , m_row(init_rowcol.row_or(1))
        , m_column(init_rowcol.column_or(1))
        , m_text(text)
        , m_origin(origin)
    {
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

    void ParserBase::add_error(msg::LocalizedString message, const SourceLoc& loc)
    {
        // avoid cascading errors by only saving the first
        if (!m_err)
        {
            // find end of line
            auto line_end = loc.it;
            while (line_end != line_end.end() && *line_end != '\n' && *line_end != '\r')
            {
                ++line_end;
            }
            m_err = std::make_unique<ParseError>(
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
