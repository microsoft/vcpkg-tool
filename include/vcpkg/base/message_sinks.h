#pragma once

#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>

#include <mutex>

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
        inline void println(Color c, LocalizedString&& s) { this->print(c, s.append_raw('\n')); }
        inline void println(LocalizedString&& s) { this->print(Color::none, s.append_raw('\n')); }
        void println_warning(const LocalizedString& s);
        void println_error(const LocalizedString& s);

        template<VCPKG_DECL_MSG_TEMPLATE>
        void print(VCPKG_DECL_MSG_ARGS)
        {
            this->print(msg::format(VCPKG_EXPAND_MSG_ARGS));
        }
        template<VCPKG_DECL_MSG_TEMPLATE>
        void println(VCPKG_DECL_MSG_ARGS)
        {
            this->println(msg::format(VCPKG_EXPAND_MSG_ARGS));
        }
        template<VCPKG_DECL_MSG_TEMPLATE>
        void println_warning(VCPKG_DECL_MSG_ARGS)
        {
            this->println_warning(msg::format(VCPKG_EXPAND_MSG_ARGS));
        }
        template<VCPKG_DECL_MSG_TEMPLATE>
        void println_error(VCPKG_DECL_MSG_ARGS)
        {
            this->println_error(msg::format(VCPKG_EXPAND_MSG_ARGS));
        }

        template<VCPKG_DECL_MSG_TEMPLATE>
        void print(Color c, VCPKG_DECL_MSG_ARGS)
        {
            this->print(c, msg::format(VCPKG_EXPAND_MSG_ARGS));
        }
        template<VCPKG_DECL_MSG_TEMPLATE>
        void println(Color c, VCPKG_DECL_MSG_ARGS)
        {
            this->println(c, msg::format(VCPKG_EXPAND_MSG_ARGS));
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

    struct BGMessageSink : MessageSink
    {
        BGMessageSink(MessageSink& out_sink) : out_sink(out_sink) { }
        ~BGMessageSink() { publish_directly_to_out_sink(); }
        // must be called from producer
        void print(Color c, StringView sv) override;

        // must be called from consumer (synchronizer of out)
        void print_published();

        void publish_directly_to_out_sink();

    private:
        MessageSink& out_sink;

        std::mutex m_lock;
        // guarded by m_lock
        std::vector<std::pair<Color, std::string>> m_published;
        // buffers messages until newline is reached
        // guarded by m_print_directly_lock
        std::vector<std::pair<Color, std::string>> m_unpublished;

        std::mutex m_print_directly_lock;
        bool m_print_directly_to_out_sink = false;
    };
}
