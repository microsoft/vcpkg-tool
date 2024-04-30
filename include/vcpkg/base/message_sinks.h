#pragma once

#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/messages.h>

namespace vcpkg
{

    struct MessageSink
    {
        virtual void print(Color c, StringView sv) = 0;

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

    struct CombiningSink : MessageSink
    {
        MessageSink& m_first;
        MessageSink& m_second;
        CombiningSink(MessageSink& first, MessageSink& second) : m_first(first), m_second(second) { }
        void print(Color c, StringView sv) override;
    };
}
