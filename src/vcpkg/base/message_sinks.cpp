#include <vcpkg/base/file_sink.h>
#include <vcpkg/base/message_sinks.h>

namespace
{
    using namespace vcpkg;
    struct NullMessageSink : MessageSink
    {
        virtual void print(Color, StringView) override { }
    };

    NullMessageSink null_sink_instance;

    struct OutMessageSink : MessageSink
    {
        virtual void print(Color c, StringView sv) override { msg::write_unlocalized_text(c, sv); }
    };

    OutMessageSink out_sink_instance;

    struct StdOutMessageSink : MessageSink
    {
        virtual void print(Color c, StringView sv) override { msg::write_unlocalized_text_to_stdout(c, sv); }
    };

    StdOutMessageSink stdout_sink_instance;

    struct StdErrMessageSink : MessageSink
    {
        virtual void print(Color c, StringView sv) override { msg::write_unlocalized_text_to_stderr(c, sv); }
    };

    StdErrMessageSink stderr_sink_instance;

}

namespace vcpkg
{

    void MessageSink::println_warning(const LocalizedString& s)
    {
        println(Color::warning, format(msgWarningMessage).append(s));
    }

    void MessageSink::println_error(const LocalizedString& s)
    {
        println(Color::error, format(msgErrorMessage).append(s));
    }

    MessageSink& null_sink = null_sink_instance;
    MessageSink& out_sink = out_sink_instance;
    MessageSink& stderr_sink = stderr_sink_instance;
    MessageSink& stdout_sink = stdout_sink_instance;

    void FileSink::print(Color, StringView sv)
    {
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               m_out_file.write(sv.data(), 1, sv.size()) == sv.size(),
                               msgErrorWhileWriting,
                               msg::path = m_log_file);
    }

    void CombiningSink::print(Color c, StringView sv)
    {
        m_first.print(c, sv);
        m_second.print(c, sv);
    }

}
