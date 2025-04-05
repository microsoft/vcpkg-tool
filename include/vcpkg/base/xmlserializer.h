#pragma once

#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg
{
    struct XmlSerializer
    {
        XmlSerializer& emit_declaration();
        XmlSerializer& open_tag(StringLiteral sl);
        XmlSerializer& start_complex_open_tag(StringLiteral sl);
        XmlSerializer& text_attr(StringLiteral name, StringView content);
        template<class T>
        XmlSerializer& attr(StringLiteral name, const T& content)
        {
            return text_attr(name, content);
        }
        XmlSerializer& finish_complex_open_tag();
        XmlSerializer& finish_self_closing_complex_tag();
        XmlSerializer& close_tag(StringLiteral sl);
        XmlSerializer& text(StringView sv);
        XmlSerializer& cdata(StringView sv);
        XmlSerializer& simple_tag(StringLiteral tag, StringView content);
        XmlSerializer& line_break();

        std::string buf;

    private:
        XmlSerializer& emit_pending_indent();

        int m_indent = 0;
        bool m_pending_indent = false;
    };
}
