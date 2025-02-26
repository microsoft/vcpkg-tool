#include <vcpkg/base/file_sink.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/strings.h>

namespace
{
    using namespace vcpkg;
    struct NullMessageSink : MessageSink
    {
        virtual void println(const MessageLine&) override { }
        virtual void println(MessageLine&&) override { }
        virtual void println(const LocalizedString&) override { }
        virtual void println(LocalizedString&&) override { }
        virtual void println(Color, const LocalizedString&) override { }
        virtual void println(Color, LocalizedString&&) override { }
    };

    NullMessageSink null_sink_instance;

    struct OutMessageSink final : MessageSink
    {
        virtual void println(const MessageLine& line) override
        {
            for (auto&& segment : line.get_segments())
            {
                msg::write_unlocalized_text(segment.color, segment.text);
            }

            msg::write_unlocalized_text(Color::none, "\n");
        }

        virtual void println(MessageLine&& line) override
        {
            auto& segments = line.get_segments();
            if (segments.empty())
            {
                msg::write_unlocalized_text(Color::none, "\n");
                return;
            }

            line.print(segments.back().color, "\n");
            for (auto&& segment : line.get_segments())
            {
                msg::write_unlocalized_text(segment.color, segment.text);
            }
        }
        virtual void println(const LocalizedString& text) override
        {
            msg::write_unlocalized_text(Color::none, text);
            msg::write_unlocalized_text(Color::none, "\n");
        }
        virtual void println(LocalizedString&& text) override
        {
            text.append_raw('\n');
            msg::write_unlocalized_text(Color::none, text);
        }
        virtual void println(Color color, const LocalizedString& text) override
        {
            msg::write_unlocalized_text(color, text);
            msg::write_unlocalized_text(Color::none, "\n");
        }
        virtual void println(Color color, LocalizedString&& text) override
        {
            text.append_raw('\n');
            msg::write_unlocalized_text(color, text);
        }
    };

    OutMessageSink out_sink_instance;

    struct StdOutMessageSink final : MessageSink
    {
        virtual void println(const MessageLine& line) override
        {
            for (auto&& segment : line.get_segments())
            {
                msg::write_unlocalized_text_to_stdout(segment.color, segment.text);
            }

            msg::write_unlocalized_text_to_stdout(Color::none, "\n");
        }

        virtual void println(MessageLine&& line) override
        {
            auto& segments = line.get_segments();
            if (segments.empty())
            {
                msg::write_unlocalized_text_to_stdout(Color::none, "\n");
                return;
            }

            line.print(segments.back().color, "\n");
            for (auto&& segment : line.get_segments())
            {
                msg::write_unlocalized_text_to_stdout(segment.color, segment.text);
            }
        }
        virtual void println(const LocalizedString& text) override
        {
            msg::write_unlocalized_text_to_stdout(Color::none, text);
            msg::write_unlocalized_text_to_stdout(Color::none, "\n");
        }
        virtual void println(LocalizedString&& text) override
        {
            text.append_raw('\n');
            msg::write_unlocalized_text_to_stdout(Color::none, text);
        }
        virtual void println(Color color, const LocalizedString& text) override
        {
            msg::write_unlocalized_text_to_stdout(color, text);
            msg::write_unlocalized_text_to_stdout(color, "\n");
        }
        virtual void println(Color color, LocalizedString&& text) override
        {
            text.append_raw('\n');
            msg::write_unlocalized_text_to_stdout(color, text);
        }
    };

    StdOutMessageSink stdout_sink_instance;

    struct StdErrMessageSink final : MessageSink
    {
        virtual void println(const MessageLine& line) override
        {
            for (auto&& segment : line.get_segments())
            {
                msg::write_unlocalized_text_to_stderr(segment.color, segment.text);
            }

            msg::write_unlocalized_text_to_stderr(Color::none, "\n");
        }

        virtual void println(MessageLine&& line) override
        {
            auto& segments = line.get_segments();
            if (segments.empty())
            {
                msg::write_unlocalized_text_to_stderr(Color::none, "\n");
                return;
            }

            line.print(segments.back().color, "\n");
            for (auto&& segment : line.get_segments())
            {
                msg::write_unlocalized_text_to_stderr(segment.color, segment.text);
            }
        }
        virtual void println(const LocalizedString& text) override
        {
            msg::write_unlocalized_text_to_stderr(Color::none, text);
            msg::write_unlocalized_text_to_stderr(Color::none, "\n");
        }
        virtual void println(LocalizedString&& text) override
        {
            text.append_raw('\n');
            msg::write_unlocalized_text_to_stderr(Color::none, text);
        }
        virtual void println(Color color, const LocalizedString& text) override
        {
            msg::write_unlocalized_text_to_stderr(color, text);
            msg::write_unlocalized_text_to_stderr(color, "\n");
        }
        virtual void println(Color color, LocalizedString&& text) override
        {
            text.append_raw('\n');
            msg::write_unlocalized_text_to_stderr(color, text);
        }
    };

    StdErrMessageSink stderr_sink_instance;

}

namespace vcpkg
{
    MessageLine::MessageLine(const LocalizedString& ls) { segments.push_back({Color::none, ls.data()}); }
    MessageLine::MessageLine(LocalizedString&& ls) { segments.push_back({Color::none, std::move(ls).extract_data()}); }
    void MessageLine::print(Color color, StringView text)
    {
        if (!segments.empty() && segments.back().color == color)
        {
            segments.back().text.append(text.data(), text.size());
        }
        else
        {
            segments.push_back({color, std::string(text)});
        }
    }
    void MessageLine::print(StringView text) { print(Color::none, text); }
    const std::vector<MessageLineSegment>& MessageLine::get_segments() const noexcept { return segments; }

    std::string MessageLine::to_string() const { return adapt_to_string(*this); }
    void MessageLine::to_string(std::string& target) const
    {
        for (const auto& segment : segments)
        {
            target.append(segment.text);
        }
    }

    void MessageSink::println(const LocalizedString& s)
    {
        MessageLine line;
        line.print(Color::none, s);
        this->println(line);
    }

    void MessageSink::println(LocalizedString&& s) { this->println(s); }

    void MessageSink::println(Color c, const LocalizedString& s)
    {
        MessageLine line;
        line.print(c, s);
        this->println(line);
    }

    void MessageSink::println(Color c, LocalizedString&& s) { this->println(c, s); }

    MessageSink& null_sink = null_sink_instance;
    MessageSink& out_sink = out_sink_instance;
    MessageSink& stderr_sink = stderr_sink_instance;
    MessageSink& stdout_sink = stdout_sink_instance;

    void FileSink::println(const MessageLine& line)
    {
        std::string whole_line;
        line.to_string(whole_line);
        whole_line.push_back('\n');
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               m_out_file.write(whole_line.data(), 1, whole_line.size()) == whole_line.size(),
                               msgErrorWhileWriting,
                               msg::path = m_log_file);
    }

    void FileSink::println(MessageLine&& line)
    {
        line.print("\n");
        for (auto&& segment : line.get_segments())
        {
            Checks::msg_check_exit(VCPKG_LINE_INFO,
                                   m_out_file.write(segment.text.data(), 1, segment.text.size()) == segment.text.size(),
                                   msgErrorWhileWriting,
                                   msg::path = m_log_file);
        }
    }

    void TeeSink::println(const MessageLine& line)
    {
        m_first.println(line);
        m_second.println(line);
    }

    void TeeSink::println(MessageLine&& line)
    {
        m_first.println(line);
        m_second.println(std::move(line));
    }

    void TeeSink::println(const LocalizedString& line)
    {
        m_first.println(line);
        m_second.println(line);
    }

    void TeeSink::println(LocalizedString&& line)
    {
        m_first.println(line);
        m_second.println(std::move(line));
    }

    void TeeSink::println(Color color, const LocalizedString& line)
    {
        m_first.println(color, line);
        m_second.println(color, line);
    }

    void TeeSink::println(Color color, LocalizedString&& line)
    {
        m_first.println(color, line);
        m_second.println(color, std::move(line));
    }

    void BGMessageSink::println(const MessageLine& line)
    {
        std::lock_guard<std::mutex> lk(m_published_lock);
        if (m_print_directly_to_out_sink)
        {
            out_sink.println(line);
            return;
        }

        m_published.push_back(line);
    }

    void BGMessageSink::println(MessageLine&& line)
    {
        std::lock_guard<std::mutex> lk(m_published_lock);
        if (m_print_directly_to_out_sink)
        {
            out_sink.println(std::move(line));
            return;
        }

        m_published.push_back(std::move(line));
    }

    void BGMessageSink::print_published()
    {
        std::vector<MessageLine> tmp;
        for (;;)
        {
            {
                std::lock_guard<std::mutex> lk(m_published_lock);
                swap(tmp, m_published);
            }

            if (tmp.empty())
            {
                return;
            }

            for (auto&& line : tmp)
            {
                out_sink.println(std::move(line));
            }

            tmp.clear();
        }
    }

    void BGMessageSink::publish_directly_to_out_sink()
    {
        std::lock_guard<std::mutex> lk(m_published_lock);

        m_print_directly_to_out_sink = true;
        for (auto&& msg : m_published)
        {
            out_sink.println(std::move(msg));
        }

        m_published.clear();
    }
}
