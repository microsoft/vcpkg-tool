#include <vcpkg/base/cmd-parser.h>
#include <vcpkg/base/strings.h>

#include <stdint.h>

#include <algorithm>

namespace
{
    using namespace vcpkg;

    static void help_table_newline_indent(std::string& target)
    {
        target.push_back('\n');
        target.append(34, ' ');
    }

    static constexpr ptrdiff_t S_MAX_LINE_LENGTH = 100;
}

namespace vcpkg
{
    void HelpTableFormatter::format(StringView col1, StringView col2)
    {
        static constexpr std::size_t initial_space = 2;
        static constexpr std::size_t col1_capacity = 31;
        static constexpr std::size_t seperating_space = 1;

        m_str.append(initial_space, ' ');
        Strings::append(m_str, col1);
        if (col1.size() > col1_capacity)
        {
            help_table_newline_indent(m_str);
        }
        else
        {
            m_str.append((col1_capacity + seperating_space) - col1.size(), ' ');
        }
        text(col2, initial_space + col1_capacity + seperating_space);

        m_str.push_back('\n');
    }

    void HelpTableFormatter::header(StringView name)
    {
        m_str.append(name.data(), name.size());
        m_str.push_back(':');
        m_str.push_back('\n');
    }

    void HelpTableFormatter::example(StringView example_text)
    {
        m_str.append(example_text.data(), example_text.size());
        m_str.push_back('\n');
    }

    void HelpTableFormatter::blank() { m_str.push_back('\n'); }

    // Note: this formatting code does not properly handle unicode, however all of our documentation strings are English
    // ASCII.
    void HelpTableFormatter::text(StringView text, int indent)
    {
        const char* line_start = text.begin();
        const char* const e = text.end();
        const char* best_break = std::find_if(line_start, e, [](char ch) { return ch == ' ' || ch == '\n'; });

        while (best_break != e)
        {
            const char* next_break = std::find_if(best_break + 1, e, [](char ch) { return ch == ' ' || ch == '\n'; });
            if (*best_break == '\n' || next_break - line_start + indent > S_MAX_LINE_LENGTH)
            {
                m_str.append(line_start, best_break);
                m_str.push_back('\n');
                line_start = best_break + 1;
                best_break = next_break;
                m_str.append(indent, ' ');
            }
            else
            {
                best_break = next_break;
            }
        }
        m_str.append(line_start, best_break);
    }
}
