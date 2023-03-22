#include <vcpkg/base/message_sinks.h>

namespace
{
    using namespace vcpkg;
    struct NullMessageSink : MessageSink
    {
        virtual void print(Color, StringView) override { }
    };

    NullMessageSink null_sink_instance;

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
        println(Color::warning, format(msg::msgWarningMessage).append(s));
    }

    void MessageSink::println_error(const LocalizedString& s)
    {
        println(Color::error, format(msg::msgErrorMessage).append(s));
    }

    MessageSink& null_sink = null_sink_instance;
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

    void BGMessageSink::print(Color c, StringView sv)
    {
        std::lock_guard<std::mutex> print_lk(m_print_directly_lock);
        if (m_print_directly_to_out_sink)
        {
            out_sink.print(c, sv);
            return;
        }

        std::string s = sv.to_string();
        auto pos = s.find_last_of('\n');
        if (pos != std::string::npos)
        {
            {
                std::lock_guard<std::mutex> lk(m_lock);
                m_published.insert(m_published.end(),
                                   std::make_move_iterator(m_unpublished.begin()),
                                   std::make_move_iterator(m_unpublished.end()));
                m_published.emplace_back(c, s.substr(0, pos + 1));
            }
            m_unpublished.clear();
            if (s.size() > pos + 1)
            {
                m_unpublished.emplace_back(c, s.substr(pos + 1));
            }
        }
        else
        {
            m_unpublished.emplace_back(c, std::move(s));
        }
    }

    void BGMessageSink::print_published()
    {
        std::lock_guard<std::mutex> lk(m_lock);
        for (auto&& m : m_published)
        {
            out_sink.print(m.first, m.second);
        }
        m_published.clear();
    }

    void BGMessageSink::publish_directly_to_out_sink()
    {
        std::lock_guard<std::mutex> print_lk(m_print_directly_lock);
        std::lock_guard<std::mutex> lk(m_lock);

        m_print_directly_to_out_sink = true;
        for (auto& messages : {&m_published, &m_unpublished})
        {
            for (auto&& m : *messages)
            {
                out_sink.print(m.first, m.second);
            }
            messages->clear();
        }
    }
}
