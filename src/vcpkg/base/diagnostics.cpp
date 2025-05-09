#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/fmt.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/system.debug.h>

#include <system_error>

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
        static_assert(std::size(prefixes) == static_cast<unsigned int>(DiagKind::COUNT));

        const auto diag_index = static_cast<unsigned int>(kind);
        if (diag_index >= static_cast<unsigned int>(DiagKind::COUNT))
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        const auto prefix = prefixes[diag_index];
        target.append(prefix->data(), prefix->size());
    }

    template<class LineKind>
    void joined_line_to_string(const std::vector<LineKind>& lines, std::string& target)
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

    bool diagnostic_lines_any_errors(const std::vector<DiagnosticLine>& lines)
    {
        for (auto&& line : lines)
        {
            if (line.kind() == DiagKind::Error)
            {
                return true;
            }
        }

        return false;
    }
}

namespace vcpkg
{
    void DiagnosticContext::report(DiagnosticLine&& line) { report(line); }

    void DiagnosticContext::report_system_error(StringLiteral system_api_name, int error_value)
    {
        report_error(msgSystemApiErrorMessage,
                     msg::system_api = system_api_name,
                     msg::exit_code = error_value,
                     msg::error_msg = std::system_category().message(error_value));
    }

    void DiagnosticLine::print_to(MessageSink& sink) const { sink.println(to_message_line()); }
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

    MessageLine DiagnosticLine::to_message_line() const
    {
        MessageLine ret;
        {
            std::string file_prefix;
            append_file_prefix(file_prefix, m_origin, m_position);
            ret.print(file_prefix);
        }
        switch (m_kind)
        {
            case DiagKind::None:
                // intentionally blank
                break;
            case DiagKind::Message: ret.print(MessagePrefix); break;
            case DiagKind::Error:
            {
                ret.print(Color::error, "error");
                ret.print(ColonSpace);
            }
            break;
            case DiagKind::Warning:
            {
                ret.print(Color::warning, "warning");
                ret.print(ColonSpace);
            }
            break;
            case DiagKind::Note: ret.print(NotePrefix); break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        ret.print(m_message);
        return ret;
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

    DiagnosticLine DiagnosticLine::reduce_to_warning() const&
    {
        return DiagnosticLine{
            InternalTag{}, m_kind == DiagKind::Error ? DiagKind::Warning : m_kind, m_origin, m_position, m_message};
    }
    DiagnosticLine DiagnosticLine::reduce_to_warning() &&
    {
        return DiagnosticLine{InternalTag{},
                              m_kind == DiagKind::Error ? DiagKind::Warning : m_kind,
                              std::move(m_origin),
                              m_position,
                              std::move(m_message)};
    }

    DiagnosticLine::DiagnosticLine(InternalTag,
                                   DiagKind kind,
                                   const Optional<std::string>& origin,
                                   TextRowCol position,
                                   const LocalizedString& message)
        : m_kind(kind), m_origin(origin), m_position(position), m_message(message)
    {
    }
    DiagnosticLine::DiagnosticLine(
        InternalTag, DiagKind kind, Optional<std::string>&& origin, TextRowCol position, LocalizedString&& message)
        : m_kind(kind), m_origin(std::move(origin)), m_position(position), m_message(std::move(message))
    {
    }

    void PrintingDiagnosticContext::report(const DiagnosticLine& line) { line.print_to(sink); }

    void PrintingDiagnosticContext::statusln(const LocalizedString& message) { sink.println(message); }
    void PrintingDiagnosticContext::statusln(LocalizedString&& message) { sink.println(std::move(message)); }
    void PrintingDiagnosticContext::statusln(const MessageLine& message) { sink.println(message); }
    void PrintingDiagnosticContext::statusln(MessageLine&& message) { sink.println(std::move(message)); }

    void BufferedDiagnosticContext::report(const DiagnosticLine& line) { lines.push_back(line); }
    void BufferedDiagnosticContext::report(DiagnosticLine&& line) { lines.push_back(std::move(line)); }
    void BufferedDiagnosticContext::print_to(MessageSink& sink) const
    {
        for (auto&& line : lines)
        {
            line.print_to(sink);
        }
    }

    // Converts this message into a string
    // Prefer print() if possible because it applies color
    // Not safe to use in the face of concurrent calls to report()
    std::string BufferedDiagnosticContext::to_string() const { return adapt_to_string(*this); }
    void BufferedDiagnosticContext::to_string(std::string& target) const { joined_line_to_string(lines, target); }

    bool BufferedDiagnosticContext::any_errors() const noexcept { return diagnostic_lines_any_errors(lines); }
    bool BufferedDiagnosticContext::empty() const noexcept { return lines.empty(); }

    void BufferedDiagnosticContext::statusln(const LocalizedString& message) { status_sink.println(message); }
    void BufferedDiagnosticContext::statusln(LocalizedString&& message) { status_sink.println(std::move(message)); }
    void BufferedDiagnosticContext::statusln(const MessageLine& message) { status_sink.println(message); }
    void BufferedDiagnosticContext::statusln(MessageLine&& message) { status_sink.println(std::move(message)); }

    void FullyBufferedDiagnosticContext::report(const DiagnosticLine& line) { lines.push_back(line.to_message_line()); }
    void FullyBufferedDiagnosticContext::report(DiagnosticLine&& line)
    {
        lines.push_back(std::move(line).to_message_line());
    }

    void FullyBufferedDiagnosticContext::statusln(const LocalizedString& message) { lines.emplace_back(message); }
    void FullyBufferedDiagnosticContext::statusln(LocalizedString&& message) { lines.emplace_back(std::move(message)); }
    void FullyBufferedDiagnosticContext::statusln(const MessageLine& message) { lines.emplace_back(message); }
    void FullyBufferedDiagnosticContext::statusln(MessageLine&& message) { lines.emplace_back(std::move(message)); }

    void FullyBufferedDiagnosticContext::print_to(MessageSink& sink) const
    {
        for (auto&& line : lines)
        {
            sink.println(line);
        }
    }

    std::string FullyBufferedDiagnosticContext::to_string() const { return adapt_to_string(*this); }
    void FullyBufferedDiagnosticContext::to_string(std::string& target) const { joined_line_to_string(lines, target); }

    bool FullyBufferedDiagnosticContext::empty() const noexcept { return lines.empty(); }

    void AttemptDiagnosticContext::report(const DiagnosticLine& line) { lines.push_back(line); }
    void AttemptDiagnosticContext::report(DiagnosticLine&& line) { lines.push_back(std::move(line)); }

    void AttemptDiagnosticContext::statusln(const LocalizedString& message) { inner_context.statusln(message); }
    void AttemptDiagnosticContext::statusln(LocalizedString&& message) { inner_context.statusln(std::move(message)); }
    void AttemptDiagnosticContext::statusln(const MessageLine& message) { inner_context.statusln(message); }
    void AttemptDiagnosticContext::statusln(MessageLine&& message) { inner_context.statusln(std::move(message)); }

    void AttemptDiagnosticContext::commit()
    {
        for (auto& line : lines)
        {
            inner_context.report(std::move(line));
        }

        lines.clear();
    }

    void AttemptDiagnosticContext::handle() { lines.clear(); }

    AttemptDiagnosticContext::~AttemptDiagnosticContext()
    {
        if (!lines.empty())
        {
#if defined(NDEBUG)
            for (auto& line : lines)
            {
                inner_context.report(std::move(line));
            }
#else  // ^^^ NDEBUG // !NDEBUG vvv
            Checks::unreachable(VCPKG_LINE_INFO, "Uncommitted diagnostics in ~AttemptDiagnosticContext");
#endif // ^^^ !NDEBUG
        }
    }

    void WarningDiagnosticContext::report(const DiagnosticLine& line)
    {
        inner_context.report(line.reduce_to_warning());
    }
    void WarningDiagnosticContext::report(DiagnosticLine&& line)
    {
        inner_context.report(std::move(line).reduce_to_warning());
    }

    void WarningDiagnosticContext::statusln(const LocalizedString& message) { inner_context.statusln(message); }
    void WarningDiagnosticContext::statusln(LocalizedString&& message) { inner_context.statusln(std::move(message)); }
    void WarningDiagnosticContext::statusln(const MessageLine& message) { inner_context.statusln(message); }
    void WarningDiagnosticContext::statusln(MessageLine&& message) { inner_context.statusln(std::move(message)); }
} // namespace vcpkg

namespace
{
    struct NullDiagnosticContext final : DiagnosticContext
    {
        // these are all intentionally empty
        virtual void report(const DiagnosticLine&) override { }
        virtual void statusln(const LocalizedString&) override { }
        virtual void statusln(LocalizedString&&) override { }
        virtual void statusln(const MessageLine&) override { }
        virtual void statusln(MessageLine&&) override { }
    };

    NullDiagnosticContext null_diagnostic_context_instance;

    struct ConsoleDiagnosticContext final : DiagnosticContext
    {
        virtual void report(const DiagnosticLine& line) override { line.print_to(out_sink); }
        virtual void statusln(const LocalizedString& message) override { out_sink.println(message); }
        virtual void statusln(LocalizedString&& message) override { out_sink.println(std::move(message)); }
        virtual void statusln(const MessageLine& message) override { out_sink.println(message); }
        virtual void statusln(MessageLine&& message) override { out_sink.println(std::move(message)); }
    };

    ConsoleDiagnosticContext console_diagnostic_context_instance;

    struct StatusOnlyDiagnosticContext final : DiagnosticContext
    {
        virtual void report(const DiagnosticLine&) override { }
        virtual void statusln(const LocalizedString& message) override { out_sink.println(message); }
        virtual void statusln(LocalizedString&& message) override { out_sink.println(std::move(message)); }
        virtual void statusln(const MessageLine& message) override { out_sink.println(message); }
        virtual void statusln(MessageLine&& message) override { out_sink.println(std::move(message)); }
    };

    StatusOnlyDiagnosticContext status_only_diagnostic_context_instance;
} // unnamed namespace

namespace vcpkg
{
    DiagnosticContext& null_diagnostic_context = null_diagnostic_context_instance;
    DiagnosticContext& console_diagnostic_context = console_diagnostic_context_instance;
    DiagnosticContext& status_only_diagnostic_context = status_only_diagnostic_context_instance;
}
