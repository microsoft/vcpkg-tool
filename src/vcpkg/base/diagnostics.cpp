#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/fmt.h>
#include <vcpkg/base/message_sinks.h>

#include <fmt/compile.h>

using namespace vcpkg;

namespace vcpkg
{
    void DiagnosticLine::print(MessageSink& sink) const { sink.println(LocalizedString::from_raw(this->to_string())); }
    std::string DiagnosticLine::to_string() const
    {
        std::string result;
        this->to_string(result);
        return result;
    }
    void DiagnosticLine::to_string(std::string& target) const
    {
        // file:line:col: kind: message
        if (auto origin = m_origin.get())
        {
            target.append(*origin);
            if (m_position.row)
            {
                target.push_back(':');
                fmt::format_to(std::back_inserter(target), FMT_COMPILE("{}"), m_position.row);

                if (m_position.column)
                {
                    target.push_back(':');
                    fmt::format_to(std::back_inserter(target), FMT_COMPILE("{}"), m_position.column);
                }
            }

            static constexpr StringLiteral ColonSpace{": "};
            target.append(ColonSpace.data(), ColonSpace.size());
        }

        static constexpr StringLiteral Empty{""};
        static constexpr const StringLiteral* prefixes[] = {
            &Empty, &MessagePrefix, &ErrorPrefix, &WarningPrefix, &NotePrefix};

        const auto diag_index = static_cast<unsigned int>(m_kind);
        if (diag_index >= static_cast<unsigned int>(DiagKind::COUNT))
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        const auto prefix = prefixes[diag_index];
        target.append(prefix->data(), prefix->size());
        target.append(m_message.data());
    }

    void BufferedDiagnosticContext::report(const DiagnosticLine& line)
    {
        std::lock_guard lck{m_mtx};
        lines.push_back(line);
    }

    void BufferedDiagnosticContext::report(DiagnosticLine&& line)
    {
        std::lock_guard lck{m_mtx};
        lines.push_back(std::move(line));
    }
    void BufferedDiagnosticContext::print(MessageSink& sink) const { sink.println(LocalizedString::from_raw("hello")); }
    // Converts this message into a string
    // Prefer print() if possible because it applies color
    // Not safe to use in the face of concurrent calls to report()
    std::string BufferedDiagnosticContext::to_string() const
    {
        std::string result;
        this->to_string(result);
        return result;
    }
    void BufferedDiagnosticContext::to_string(std::string& target) const
    {
        auto first = lines.begin();
        const auto last = lines.end();
        if (first == last)
        {
            return;
        }

        for (;;)
        {
            first->to_string(target);
            if (++first == last)
            {
                return;
            }

            target.push_back('\n');
        }
    }
}
