#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/stringview.h>

#include <functional>
#include <system_error>
#include <type_traits>

namespace vcpkg
{
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
        T* get() noexcept { return &t; }
        const T* get() const noexcept { return &t; }
        T t;
    };
    template<class T>
    struct ExpectedHolder<T&>
    {
        ExpectedHolder(T& t) : t(&t) { }
        ExpectedHolder() : t(nullptr) { }
        using pointer = T*;
        using const_pointer = T*;
        T* get() noexcept { return t; }
        T* get() const noexcept { return t; }
        T* t;
    };

    template<class T, class S>
    struct ExpectedT
    {
        constexpr ExpectedT() = default;

        // Constructors are intentionally implicit

        ExpectedT(const S& s, ExpectedRightTag = {}) : value_is_error(true), m_s(s), m_t() { }
        template<class U = S, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
        ExpectedT(S&& s, ExpectedRightTag = {}) : value_is_error(true), m_s(std::move(s)), m_t()
        {
        }

        ExpectedT(const T& t, ExpectedLeftTag = {}) : value_is_error(false), m_s(), m_t(t) { }
        template<class U = T, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
        ExpectedT(T&& t, ExpectedLeftTag = {}) : value_is_error(false), m_s(), m_t(std::move(t))
        {
        }

        ExpectedT(const ExpectedT&) = default;
        ExpectedT(ExpectedT&&) = default;
        ExpectedT& operator=(const ExpectedT&) = default;
        ExpectedT& operator=(ExpectedT&&) = default;

        explicit constexpr operator bool() const noexcept { return !value_is_error; }
        constexpr bool has_value() const noexcept { return !value_is_error; }

        const T&& value_or_exit(const LineInfo& line_info) const&&
        {
            exit_if_error(line_info);
            return std::move(*m_t.get());
        }

        T&& value_or_exit(const LineInfo& line_info) &&
        {
            exit_if_error(line_info);
            return std::move(*m_t.get());
        }

        T& value_or_exit(const LineInfo& line_info) &
        {
            exit_if_error(line_info);
            return *m_t.get();
        }

        const T& value_or_exit(const LineInfo& line_info) const&
        {
            exit_if_error(line_info);
            return *m_t.get();
        }

        const S& error() const&
        {
            exit_if_not_error();
            return m_s;
        }

        S&& error() &&
        {
            exit_if_not_error();
            return std::move(m_s);
        }

        std::string error_to_string() const { return fmt::format("{}", error()); }

        typename ExpectedHolder<T>::const_pointer get() const
        {
            if (value_is_error)
            {
                return nullptr;
            }

            return m_t.get();
        }

        typename ExpectedHolder<T>::pointer get()
        {
            if (value_is_error)
            {
                return nullptr;
            }

            return m_t.get();
        }

        template<class F>
        using map_t = decltype(std::declval<F&>()(*std::declval<typename ExpectedHolder<T>::const_pointer>()));

        template<class F>
        ExpectedT<map_t<F>, S> map(F f) const&
        {
            if (value_is_error)
            {
                return ExpectedT<map_t<F>, S>{m_s, expected_right_tag};
            }
            else
            {
                return {f(*m_t.get()), expected_left_tag};
            }
        }

        template<class F>
        using move_map_t =
            decltype(std::declval<F&>()(std::move(*std::declval<typename ExpectedHolder<T>::pointer>())));

        template<class F>
        ExpectedT<move_map_t<F>, S> map(F f) &&
        {
            if (value_is_error)
            {
                return ExpectedT<move_map_t<F>, S>{std::move(m_s), expected_right_tag};
            }
            else
            {
                return {f(std::move(*m_t.get())), expected_left_tag};
            }
        }

        template<class F>
        ExpectedT& replace_error(F&& specific_error_generator)
        {
            if (value_is_error)
            {
                m_s = std::forward<F>(specific_error_generator)();
            }

            return *this;
        }

        using const_ref_type = decltype(*std::declval<typename ExpectedHolder<T>::const_pointer>());
        using move_ref_type = decltype(std::move(*std::declval<typename ExpectedHolder<T>::pointer>()));
        template<class F, class... Args>
        std::invoke_result_t<F, const_ref_type, Args&&...> then(F f, Args&&... args) const&
        {
            if (value_is_error)
            {
                return std::invoke_result_t<F, const_ref_type, Args&&...>(m_s, expected_right_tag);
            }
            else
            {
                return std::invoke(f, *m_t.get(), static_cast<Args&&>(args)...);
            }
        }

        template<class F, class... Args>
        std::invoke_result_t<F, move_ref_type, Args&&...> then(F f, Args&&... args) &&
        {
            if (value_is_error)
            {
                return std::invoke_result_t<F, move_ref_type, Args&&...>(std::move(m_s), expected_right_tag);
            }
            else
            {
                return std::invoke(f, std::move(*m_t.get()), static_cast<Args&&>(args)...);
            }
        }

    private:
        void exit_if_error(const LineInfo& line_info) const
        {
            if (value_is_error)
            {
                Checks::exit_with_message(line_info, error_to_string());
            }
        }

        void exit_if_not_error() const
        {
            if (!value_is_error)
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        bool value_is_error = false;
        S m_s;
        ExpectedHolder<T> m_t;
    };

    template<class T>
    using ExpectedS = ExpectedT<T, std::string>;
}
