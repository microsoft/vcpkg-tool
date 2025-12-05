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

    void BasicBufferedDiagnosticContext::report(const DiagnosticLine& line) { lines.push_back(line); }
    void BasicBufferedDiagnosticContext::report(DiagnosticLine&& line) { lines.push_back(std::move(line)); }
    void BasicBufferedDiagnosticContext::print_to(MessageSink& sink) const
    {
        for (auto&& line : lines)
        {
            line.print_to(sink);
        }
    }

    // Converts this message into a string
    // Prefer print() if possible because it applies color
    // Not safe to use in the face of concurrent calls to report()
    std::string BasicBufferedDiagnosticContext::to_string() const { return adapt_to_string(*this); }
    void BasicBufferedDiagnosticContext::to_string(std::string& target) const { joined_line_to_string(lines, target); }

    bool BasicBufferedDiagnosticContext::any_errors() const noexcept { return diagnostic_lines_any_errors(lines); }
    bool BasicBufferedDiagnosticContext::empty() const noexcept { return lines.empty(); }

    void SinkBufferedDiagnosticContext::statusln(const LocalizedString& message) { status_sink.println(message); }
    void SinkBufferedDiagnosticContext::statusln(LocalizedString&& message) { status_sink.println(std::move(message)); }
    void SinkBufferedDiagnosticContext::statusln(const MessageLine& message) { status_sink.println(message); }
    void SinkBufferedDiagnosticContext::statusln(MessageLine&& message) { status_sink.println(std::move(message)); }

    void ContextBufferedDiagnosticContext::statusln(const LocalizedString& message)
    {
        status_context.statusln(message);
    }
    void ContextBufferedDiagnosticContext::statusln(LocalizedString&& message)
    {
        status_context.statusln(std::move(message));
    }
    void ContextBufferedDiagnosticContext::statusln(const MessageLine& message) { status_context.statusln(message); }
    void ContextBufferedDiagnosticContext::statusln(MessageLine&& message)
    {
        status_context.statusln(std::move(message));
    }

    DiagnosticOrMessageLine::DiagnosticOrMessageLine(const DiagnosticLine& dl) : dl(dl), is_diagnostic(true) { }
    DiagnosticOrMessageLine::DiagnosticOrMessageLine(DiagnosticLine&& dl) : dl(std::move(dl)), is_diagnostic(true) { }
    DiagnosticOrMessageLine::DiagnosticOrMessageLine(const MessageLine& ml) : ml(ml), is_diagnostic(false) { }
    DiagnosticOrMessageLine::DiagnosticOrMessageLine(MessageLine&& ml) : ml(std::move(ml)), is_diagnostic(false) { }

    DiagnosticOrMessageLine::DiagnosticOrMessageLine(const DiagnosticOrMessageLine& other)
        : is_diagnostic(other.is_diagnostic)
    {
        if (is_diagnostic)
            new (&dl) DiagnosticLine(other.dl);
        else
            new (&ml) MessageLine(other.ml);
    }

    DiagnosticOrMessageLine::DiagnosticOrMessageLine(DiagnosticOrMessageLine&& other)
        : is_diagnostic(other.is_diagnostic)
    {
        if (is_diagnostic)
            new (&dl) DiagnosticLine(std::move(other.dl));
        else
            new (&ml) MessageLine(std::move(other.ml));
    }

    DiagnosticOrMessageLine::~DiagnosticOrMessageLine()
    {
        if (is_diagnostic)
        {
            dl.~DiagnosticLine();
        }
        else
        {
            ml.~MessageLine();
        }
    }

    std::string DiagnosticOrMessageLine::to_string() const { return adapt_to_string(*this); }
    void DiagnosticOrMessageLine::to_string(std::string& target) const
    {
        if (is_diagnostic)
        {
            dl.to_string(target);
        }
        else
        {
            ml.to_string(target);
        }
    }

    void FullyBufferedDiagnosticContext::report(const DiagnosticLine& line) { lines.push_back(line); }
    void FullyBufferedDiagnosticContext::report(DiagnosticLine&& line) { lines.push_back(std::move(line)); }

    void FullyBufferedDiagnosticContext::statusln(const LocalizedString& message)
    {
        lines.emplace_back(MessageLine{message});
    }
    void FullyBufferedDiagnosticContext::statusln(LocalizedString&& message)
    {
        lines.emplace_back(MessageLine{std::move(message)});
    }
    void FullyBufferedDiagnosticContext::statusln(const MessageLine& message) { lines.emplace_back(message); }
    void FullyBufferedDiagnosticContext::statusln(MessageLine&& message) { lines.emplace_back(std::move(message)); }

    void FullyBufferedDiagnosticContext::print_to(MessageSink& sink) const
    {
        for (auto&& line : lines)
        {
            if (line.is_diagnostic)
            {
                line.dl.print_to(sink);
            }
            else
            {
                sink.println(line.ml);
            }
        }
    }

    std::string FullyBufferedDiagnosticContext::to_string() const { return adapt_to_string(*this); }
    void FullyBufferedDiagnosticContext::to_string(std::string& target) const { joined_line_to_string(lines, target); }

    bool FullyBufferedDiagnosticContext::empty() const noexcept { return lines.empty(); }

    void FullyBufferedDiagnosticContext::report_to(DiagnosticContext& context) const&
    {
        for (auto&& line : lines)
        {
            if (line.is_diagnostic)
            {
                context.report(line.dl);
            }
            else
            {
                context.statusln(line.ml);
            }
        }
    }

    void FullyBufferedDiagnosticContext::report_to(DiagnosticContext& context) &&
    {
        for (auto&& line : lines)
        {
            if (line.is_diagnostic)
            {
                context.report(std::move(line.dl));
            }
            else
            {
                context.statusln(std::move(line.ml));
            }
        }
    }

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
    PrintingDiagnosticContext console_diagnostic_context_instance{out_sink};
    PrintingDiagnosticContext stderr_diagnostic_context_instance{stderr_sink};

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
    DiagnosticContext& stderr_diagnostic_context = stderr_diagnostic_context_instance;
    DiagnosticContext& status_only_diagnostic_context = status_only_diagnostic_context_instance;
}
