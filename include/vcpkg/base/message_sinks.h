#pragma once

#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/messages.h>

#include <mutex>
#include <string>
#include <vector>

namespace vcpkg
{
    struct MessageLineSegment
    {
        Color color;
        std::string text;
    };

    struct MessageLine
    {
        MessageLine() = default;
        MessageLine(const MessageLine&) = default;
        MessageLine(MessageLine&&) = default;

        explicit MessageLine(const LocalizedString& ls);
        explicit MessageLine(LocalizedString&& ls);

        void print(Color color, StringView text);
        void print(StringView text);
        const std::vector<MessageLineSegment>& get_segments() const noexcept;

        std::string to_string() const;
        void to_string(std::string& target) const;

    private:
        std::vector<MessageLineSegment> segments;
    };

    struct MessageSink
    {
        virtual void println(const MessageLine& line) = 0;
        virtual void println(MessageLine&& line) = 0;

        virtual void println(const LocalizedString& s);
        virtual void println(LocalizedString&& s);

        virtual void println(Color c, const LocalizedString& s);
        virtual void println(Color c, LocalizedString&& s);

        template<VCPKG_DECL_MSG_TEMPLATE>
        void println(VCPKG_DECL_MSG_ARGS)
        {
            this->println(msg::format(VCPKG_EXPAND_MSG_ARGS));
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

    struct TeeSink final : MessageSink
    {
        MessageSink& m_first;
        MessageSink& m_second;
        TeeSink(MessageSink& first, MessageSink& second) : m_first(first), m_second(second) { }

        virtual void println(const MessageLine& line) override;
        virtual void println(MessageLine&& line) override;
        virtual void println(const LocalizedString& line) override;
        virtual void println(LocalizedString&& line) override;
        virtual void println(Color color, const LocalizedString& line) override;
        virtual void println(Color color, LocalizedString&& line) override;
    };

    struct BGMessageSink final : MessageSink
    {
        BGMessageSink(MessageSink& out_sink) : out_sink(out_sink) { }
        ~BGMessageSink() { publish_directly_to_out_sink(); }
        // must be called from producer
        virtual void println(const MessageLine& line) override;
        virtual void println(MessageLine&& line) override;
        using MessageSink::println;

        // must be called from consumer (synchronizer of out)
        void print_published();

        void publish_directly_to_out_sink();

    private:
        MessageSink& out_sink;

        std::mutex m_published_lock;
        std::vector<MessageLine> m_published;
        bool m_print_directly_to_out_sink = false;
    };
}
