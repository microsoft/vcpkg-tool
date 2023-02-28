#pragma once

#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>

namespace vcpkg
{

    struct MessageSink
    {
        virtual void print(Color c, StringView sv) = 0;

        void println() { this->print(Color::none, "\n"); }
        void print(const LocalizedString& s) { this->print(Color::none, s); }
        void println(Color c, const LocalizedString& s)
        {
            this->print(c, s);
            this->print(Color::none, "\n");
        }
        inline void println(const LocalizedString& s)
        {
            this->print(Color::none, s);
            this->print(Color::none, "\n");
        }

        template<class Message, class... Ts>
        typename Message::is_message_type print(Message m, Ts... args)
        {
            this->print(Color::none, msg::format(m, args...));
        }

        template<class Message, class... Ts>
        typename Message::is_message_type println(Message m, Ts... args)
        {
            this->print(Color::none, msg::format(m, args...).append_raw('\n'));
        }

        template<class Message, class... Ts>
        typename Message::is_message_type print(Color c, Message m, Ts... args)
        {
            this->print(c, msg::format(m, args...));
        }

        template<class Message, class... Ts>
        typename Message::is_message_type println(Color c, Message m, Ts... args)
        {
            this->print(c, msg::format(m, args...).append_raw('\n'));
        }

        void println_warning(const LocalizedString& s);
        template<class Message, class... Ts>
        typename Message::is_message_type println_warning(Message m, Ts... args)
        {
            println_warning(msg::format(m, args...));
        }

        void println_error(const LocalizedString& s);
        template<class Message, class... Ts>
        typename Message::is_message_type println_error(Message m, Ts... args)
        {
            println_error(msg::format(m, args...));
        }

        template<class Message, class... Ts, class = typename Message::is_message_type>
        LocalizedString format_warning(Message m, Ts... args)
        {
            return format(msg::msgWarningMessage).append(m, args...);
        }

        template<class Message, class... Ts, class = typename Message::is_message_type>
        LocalizedString format_error(Message m, Ts... args)
        {
            return format(msg::msgErrorMessage).append(m, args...);
        }

        MessageSink(const MessageSink&) = delete;
        MessageSink& operator=(const MessageSink&) = delete;

    protected:
        MessageSink() = default;
        ~MessageSink() = default;
    };

    struct FileSink : MessageSink
    {
        Path m_log_file;
        WriteFilePointer m_out_file;

        FileSink(Filesystem& fs, StringView log_file, Append append_to_file)
            : m_log_file(log_file), m_out_file(fs.open_for_write(m_log_file, append_to_file, VCPKG_LINE_INFO))
        {
        }
        void print(Color c, StringView sv) override;
    };
    struct CombiningSink : MessageSink
    {
        MessageSink& m_first;
        MessageSink& m_second;
        CombiningSink(MessageSink& first, MessageSink& second) : m_first(first), m_second(second) { }
        void print(Color c, StringView sv) override;
    };
}
