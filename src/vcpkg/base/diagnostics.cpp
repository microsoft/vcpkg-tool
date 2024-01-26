#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/fmt.h>
#include <vcpkg/base/message_sinks.h>

#include <fmt/compile.h>

using namespace vcpkg;

namespace
{
    static constexpr StringLiteral ColonSpace{": "};

    void append_file_prefix(std::string& target, const Optional<std::string>& maybe_origin, const TextRowCol& position)
    {
        // file:line:col: kind: message
        if (auto origin = maybe_origin.get())
        {
            target.append(*origin);
            if (position.row)
            {
                target.push_back(':');
                fmt::format_to(std::back_inserter(target), FMT_COMPILE("{}"), position.row);

                if (position.column)
                {
                    target.push_back(':');
                    fmt::format_to(std::back_inserter(target), FMT_COMPILE("{}"), position.column);
                }
            }

            target.append(ColonSpace.data(), ColonSpace.size());
        }
    }

    void append_kind_prefix(std::string& target, DiagKind kind)
    {
        static constexpr StringLiteral Empty{""};
        static constexpr const StringLiteral* prefixes[] = {
            &Empty, &MessagePrefix, &ErrorPrefix, &WarningPrefix, &NotePrefix};

        const auto diag_index = static_cast<unsigned int>(kind);
        if (diag_index >= static_cast<unsigned int>(DiagKind::COUNT))
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        const auto prefix = prefixes[diag_index];
        target.append(prefix->data(), prefix->size());
    }
}

namespace vcpkg
{
    void DiagnosticLine::print(MessageSink& sink) const
    {
        std::string buf;
        append_file_prefix(buf, m_origin, m_position);
        switch (m_kind)
        {
            case DiagKind::None:
                // intentionally blank
                break;
            case DiagKind::Message: buf.append(MessagePrefix.data(), MessagePrefix.size()); break;
            case DiagKind::Error:
            {
                sink.print(Color::none, buf);
                sink.print(Color::error, "error");
                buf.assign(ColonSpace.data(), ColonSpace.size());
            }
            break;
            case DiagKind::Warning:
            {
                sink.print(Color::none, buf);
                sink.print(Color::warning, "warning");
                buf.assign(ColonSpace.data(), ColonSpace.size());
            }
            break;
            case DiagKind::Note: buf.append(NotePrefix.data(), NotePrefix.size()); break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        buf.append(m_message.data());
        buf.push_back('\n');
        sink.print(Color::none, buf);
    }
    std::string DiagnosticLine::to_string() const
    {
        std::string result;
        this->to_string(result);
        return result;
    }
    void DiagnosticLine::to_string(std::string& target) const
    {
        append_file_prefix(target, m_origin, m_position);
        append_kind_prefix(target, m_kind);
        target.append(m_message.data());
    }

    LocalizedString DiagnosticLine::to_json_reader_string(const std::string& path, const LocalizedString& type) const
    {
        std::string result;
        append_file_prefix(result, m_origin, m_position);
        append_kind_prefix(result, m_kind);
        result.append(path);
        result.append(" (");
        result.append(type.data());
        result.append(") ");
        result.append(m_message.data());
        return LocalizedString::from_raw(result);
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
    void BufferedDiagnosticContext::print(MessageSink& sink) const
    {
        for (auto&& line : lines)
        {
            line.print(sink);
        }
    }
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
} // namespace vcpkg

namespace
{
    struct ConsoleDiagnosticContext : DiagnosticContext
    {
        std::mutex mtx;
        virtual void report(const DiagnosticLine& line) override
        {
            std::lock_guard<std::mutex> lck(mtx);
            line.print(out_sink);
        }
    };

    ConsoleDiagnosticContext console_diagnostic_context_instance;
} // unnamed namespace

namespace vcpkg
{
    DiagnosticContext& console_diagnostic_context = console_diagnostic_context_instance;
}

namespace
{
    struct NullDiagnosticContext : DiagnosticContext
    {
        virtual void report(const DiagnosticLine&) override
        {
            // intentionally empty
        }
    };

    NullDiagnosticContext null_diagnostic_context_instance;
}

namespace vcpkg
{
    DiagnosticContext& null_diagnostic_context = null_diagnostic_context_instance;
}
