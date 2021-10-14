#pragma once

#include <vcpkg/base/checks.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/stringliteral.h>

#include <system_error>
#include <type_traits>

namespace vcpkg
{
    DECLARE_MESSAGE(ExpectedValueWasError, (), "", "value was error");

    template<class Err>
    struct ErrorHolder
    {
        ErrorHolder() : m_is_error(false), m_err{} { }
        template<class U>
        ErrorHolder(U&& err) : m_is_error(true), m_err(std::forward<U>(err))
        {
        }

        constexpr bool has_error() const { return m_is_error; }

        const Err& error() const { return m_err; }
        Err& error() { return m_err; }

        msg::LocalizedString format() const { return msg::format(msgExpectedValueWasError); }

    private:
        bool m_is_error;
        Err m_err;
    };

    template<>
    struct ErrorHolder<std::string>
    {
        ErrorHolder() : m_is_error(false) { }
        template<class U>
        ErrorHolder(U&& err) : m_is_error(true), m_err(std::forward<U>(err))
        {
        }

        bool has_error() const { return m_is_error; }

        const std::string& error() const { return m_err; }
        std::string& error() { return m_err; }

        // this is a transitional implementation - this will be removed before final
        msg::LocalizedString format() const { return msg::LocalizedString::from_string_unchecked(std::string(m_err)); }

    private:
        bool m_is_error;
        std::string m_err;
    };

    template<>
    struct ErrorHolder<msg::LocalizedString>
    {
        ErrorHolder() : m_is_error(false) { }
        ErrorHolder(const msg::LocalizedString& err) : m_is_error(true), m_err(err) { }
        ErrorHolder(msg::LocalizedString&& err) : m_is_error(true), m_err(std::move(err)) { }

        bool has_error() const { return m_is_error; }

        const msg::LocalizedString& error() const { return m_err; }
        msg::LocalizedString& error() { return m_err; }

        msg::LocalizedString format() const { return m_err; }

    private:
        bool m_is_error;
        msg::LocalizedString m_err;
    };

    template<>
    struct ErrorHolder<std::error_code>
    {
        ErrorHolder() = default;
        ErrorHolder(const std::error_code& err) : m_err(err) { }

        bool has_error() const { return bool(m_err); }

        const std::error_code& error() const { return m_err; }
        std::error_code& error() { return m_err; }

        msg::LocalizedString format() const { return msg::localized_from_error_code(m_err); }

    private:
        std::error_code m_err;
    };

    struct ExpectedLeftTag
    {
    };
    struct ExpectedRightTag
    {
    };
    constexpr ExpectedLeftTag expected_left_tag;
    constexpr ExpectedRightTag expected_right_tag;

    template<class T>
    struct ExpectedHolder
    {
        ExpectedHolder() = default;
        ExpectedHolder(const T& t) : t(t) { }
        ExpectedHolder(T&& t) : t(std::move(t)) { }
        using pointer = T*;
        using const_pointer = const T*;
        T* get() { return &t; }
        const T* get() const { return &t; }
        T t;
    };
    template<class T>
    struct ExpectedHolder<T&>
    {
        ExpectedHolder(T& t) : t(&t) { }
        ExpectedHolder() : t(nullptr) { }
        using pointer = T*;
        using const_pointer = T*;
        T* get() { return t; }
        T* get() const { return t; }
        T* t;
    };

    template<class T, class S>
    struct ExpectedT
    {
        constexpr ExpectedT() = default;

        // Constructors are intentionally implicit

        ExpectedT(const S& s, ExpectedRightTag = {}) : m_s(s) { }
        template<class = std::enable_if<!std::is_reference<S>::value>>
        ExpectedT(S&& s, ExpectedRightTag = {}) : m_s(std::move(s))
        {
        }

        ExpectedT(const T& t, ExpectedLeftTag = {}) : m_t(t) { }
        template<class = std::enable_if<!std::is_reference<T>::value>>
        ExpectedT(T&& t, ExpectedLeftTag = {}) : m_t(std::move(t))
        {
        }

        ExpectedT(const ExpectedT&) = default;
        ExpectedT(ExpectedT&&) = default;
        ExpectedT& operator=(const ExpectedT&) = default;
        ExpectedT& operator=(ExpectedT&&) = default;

        // TRANSITIONAL: localization
        // these two functions exist as a stop-gap; they will be removed once ExpectedS is removed.
        template<class S2>
        operator ExpectedT<T, S2>() const&
        {
            static_assert(std::is_same<S, msg::LocalizedString>::value && std::is_same<S2, std::string>::value,
                          "This conversion function only exists to convert ExpectedL to ExpectedS");
            if (has_value())
            {
                return {*get(), expected_left_tag};
            }
            else
            {
                return {error().data(), expected_right_tag};
            }
        }
        template<class S2>
        operator ExpectedT<T, S2>() &&
        {
            static_assert(std::is_same<S, msg::LocalizedString>::value && std::is_same<S2, std::string>::value,
                          "This conversion function only exists to convert ExpectedL to ExpectedS");
            if (has_value())
            {
                return std::move(*get());
            }
            else
            {
                return error().data();
            }
        }

        explicit constexpr operator bool() const noexcept { return !m_s.has_error(); }
        constexpr bool has_value() const noexcept { return !m_s.has_error(); }

        T&& value_or_exit(const LineInfo& line_info) &&
        {
            exit_if_error(line_info);
            return std::move(*this->m_t.get());
        }

        const T& value_or_exit(const LineInfo& line_info) const&
        {
            exit_if_error(line_info);
            return *this->m_t.get();
        }

        const S& error() const& { return this->m_s.error(); }

        S&& error() && { return std::move(this->m_s.error()); }

        typename ExpectedHolder<T>::const_pointer get() const
        {
            if (!this->has_value())
            {
                return nullptr;
            }
            return this->m_t.get();
        }

        typename ExpectedHolder<T>::pointer get()
        {
            if (!this->has_value())
            {
                return nullptr;
            }
            return this->m_t.get();
        }

        template<class F>
        using map_t = decltype(std::declval<F&>()(*std::declval<typename ExpectedHolder<T>::const_pointer>()));

        template<class F, class U = map_t<F>>
        ExpectedT<U, S> map(F f) const&
        {
            if (has_value())
            {
                return {f(*m_t.get()), expected_left_tag};
            }
            else
            {
                return {error(), expected_right_tag};
            }
        }

        template<class F>
        using move_map_t =
            decltype(std::declval<F&>()(std::move(*std::declval<typename ExpectedHolder<T>::pointer>())));

        template<class F, class U = move_map_t<F>>
        ExpectedT<U, S> map(F f) &&
        {
            if (has_value())
            {
                return {f(std::move(*m_t.get())), expected_left_tag};
            }
            else
            {
                return {std::move(*this).error(), expected_right_tag};
            }
        }

        template<class F, class U = map_t<F>>
        U then(F f) const&
        {
            if (has_value())
            {
                return f(*m_t.get());
            }
            else
            {
                return U{error(), expected_right_tag};
            }
        }

        template<class F, class U = move_map_t<F>>
        U then(F f) &&
        {
            if (has_value())
            {
                return f(std::move(*m_t.get()));
            }
            else
            {
                return U{std::move(*this).error(), expected_right_tag};
            }
        }

    private:
        void exit_if_error(const LineInfo& line_info) const
        {
            if (m_s.has_error())
            {
                msg::print(append_newline(m_s.format()));
                Checks::exit_fail(line_info);
            }
        }

        ErrorHolder<S> m_s;
        ExpectedHolder<T> m_t;
    };

    template<class T>
    using Expected = ExpectedT<T, std::error_code>;

    // TRANSITIONAL: localization
    // this will be slowly removed in favor of ExpectedL
    template<class T>
    using ExpectedS = ExpectedT<T, std::string>;

    template<class T>
    using ExpectedL = ExpectedT<T, msg::LocalizedString>;
}
