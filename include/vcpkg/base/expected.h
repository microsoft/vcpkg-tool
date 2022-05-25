#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/to_string.h>

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
        ExpectedHolder() = delete;
        ExpectedHolder(const ExpectedHolder&) = default;
        ExpectedHolder(ExpectedHolder&&) = default;
        ExpectedHolder& operator=(const ExpectedHolder&) = default;
        ExpectedHolder& operator=(ExpectedHolder&&) = default;
        template<class Fwd, std::enable_if_t<!std::is_same_v<ExpectedHolder, std::remove_reference_t<Fwd>>, int> = 0>
        ExpectedHolder(Fwd&& t) : t(std::forward<Fwd>(t))
        {
        }
        using pointer = T*;
        using const_pointer = const T*;
        T* get() noexcept { return &t; }
        const T* get() const noexcept { return &t; }
        T t;
    };

    template<class T>
    struct ExpectedHolder<T&>
    {
        ExpectedHolder() = delete;
        ExpectedHolder(T& t) : t(&t) { }
        ExpectedHolder(const ExpectedHolder&) = default;
        ExpectedHolder& operator=(const ExpectedHolder&) = default;
        using pointer = T*;
        using const_pointer = T*;
        T* get() noexcept { return t; }
        T* get() const noexcept { return t; }
        T* t;
    };

    template<class T, class S>
    struct ExpectedT
    {
        // Constructors are intentionally implicit
        template<class ConvToS,
                 std::enable_if_t<std::is_convertible_v<ConvToS, S> && !std::is_same_v<T, S> &&
                                      !std::is_same_v<std::remove_reference_t<ConvToS>, T>,
                                  int> = 0>
        ExpectedT(ConvToS&& s) : m_s(std::forward<ConvToS>(s)), value_is_error(true)
        {
        }

        template<class ConvToS, std::enable_if_t<std::is_convertible_v<ConvToS, S>, int> = 0>
        ExpectedT(ConvToS&& s, ExpectedRightTag) : m_s(std::forward<ConvToS>(s)), value_is_error(true)
        {
        }

        template<class ConvToT,
                 std::enable_if_t<std::is_convertible_v<ConvToT, T> && !std::is_same_v<T, S> &&
                                      !std::is_same_v<std::remove_reference_t<ConvToT>, S>,
                                  int> = 0,
                 int = 1>
        ExpectedT(ConvToT&& t) : m_t(std::forward<ConvToT>(t)), value_is_error(false)
        {
        }

        template<class ConvToT, std::enable_if_t<std::is_convertible_v<ConvToT, T>, int> = 0>
        ExpectedT(ConvToT&& t, ExpectedLeftTag) : m_t(std::forward<ConvToT>(t)), value_is_error(false)
        {
        }

        ExpectedT(const ExpectedT& other) : value_is_error(other.value_is_error)
        {
            if (value_is_error)
            {
                ::new (&m_s) S(other.m_s);
            }
            else
            {
                ::new (&m_t) ExpectedHolder<T>(other.m_t);
            }
        }

        ExpectedT(ExpectedT&& other) : value_is_error(other.value_is_error)
        {
            if (value_is_error)
            {
                ::new (&m_s) S(std::move(other.m_s));
            }
            else
            {
                ::new (&m_t) ExpectedHolder<T>(std::move(other.m_t));
            }
        }

        // copy assign is deleted to avoid creating "valueless by exception" states
        ExpectedT& operator=(const ExpectedT& other) = delete;

        ExpectedT& operator=(ExpectedT&& other) noexcept // enforces termination
        {
            if (value_is_error)
            {
                if (other.value_is_error)
                {
                    m_s = std::move(other.m_s);
                }
                else
                {
                    m_s.~S();
                    ::new (&m_t) ExpectedHolder<T>(std::move(other.m_t));
                    value_is_error = false;
                }
            }
            else
            {
                if (other.value_is_error)
                {
                    m_t.~ExpectedHolder<T>();
                    ::new (&m_s) S(std::move(other.m_s));
                    value_is_error = true;
                }
                else
                {
                    m_t = std::move(other.m_t);
                }
            }

            return *this;
        }

        ~ExpectedT()
        {
            if (value_is_error)
            {
                m_s.~S();
            }
            else
            {
                m_t.~ExpectedHolder<T>();
            }
        }

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

        // map(F): returns an Expected<result_of_calling_F, S>
        //
        // If *this holds a value, returns an expected holding the value F(*get())
        // Otherwise, returns an expected containing a copy of error()
        template<class F>
        ExpectedT<decltype(std::declval<F&>()(std::declval<const T&>())), S> map(F f) const&
        {
            if (value_is_error)
            {
                return {m_s, expected_right_tag};
            }
            else
            {
                return {f(*m_t.get()), expected_left_tag};
            }
        }

        template<class F>
        ExpectedT<decltype(std::declval<F&>()(std::declval<T>())), S> map(F f) &&
        {
            if (value_is_error)
            {
                return {std::move(m_s), expected_right_tag};
            }
            else
            {
                return {f(std::move(*m_t.get())), expected_left_tag};
            }
        }

        // map_error(F): returns an Expected<T, result_of_calling_F>
        //
        // If *this holds a value, returns an expected holding the same value.
        // Otherwise, returns an expected containing f(error())
        template<class F>
        ExpectedT<T, decltype(std::declval<F&>()(std::declval<const S&>()))> map_error(F f) const&
        {
            if (value_is_error)
            {
                return {f(m_s), expected_right_tag};
            }
            else
            {
                return {*m_t.get(), expected_left_tag};
            }
        }

        template<class F>
        ExpectedT<T, decltype(std::declval<F&>()(std::declval<S>()))> map_error(F f) &&
        {
            if (value_is_error)
            {
                return {f(std::move(m_s)), expected_right_tag};
            }
            else
            {
                return {std::move(*m_t.get()), expected_left_tag};
            }
        }

        // then: f(T, Args...)
        // If *this contains a value, returns INVOKE(f, *get(), forward(args)...)
        // Otherwise, returns error() put into the same type.
    private:
        template<class Test>
        struct IsThenCompatibleExpected : std::false_type
        {
        };

        template<class OtherTy>
        struct IsThenCompatibleExpected<ExpectedT<OtherTy, S>> : std::true_type
        {
        };

    public:
        template<class F, class... Args>
        std::invoke_result_t<F, const T&, Args...> then(F f, Args&&... args) const&
        {
            static_assert(IsThenCompatibleExpected<std::invoke_result_t<F, const T&, Args...>>::value,
                          "then expects f to return an expected with the same error type");
            if (value_is_error)
            {
                return {m_s, expected_right_tag};
            }
            else
            {
                return std::invoke(f, *m_t.get(), static_cast<Args&&>(args)...);
            }
        }

        template<class F, class... Args>
        std::invoke_result_t<F, T&&, Args...> then(F f, Args&&... args) &&
        {
            static_assert(IsThenCompatibleExpected<std::invoke_result_t<F, T&&, Args...>>::value,
                          "then expects f to return an expected with the same error type");
            if (value_is_error)
            {
                return {m_s, expected_right_tag};
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
                Checks::exit_with_message(line_info, to_string(error()));
            }
        }

        void exit_if_not_error() const
        {
            if (!value_is_error)
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        union
        {
            S m_s;
            ExpectedHolder<T> m_t;
        };

        bool value_is_error;
    };

    template<class T>
    using ExpectedS = ExpectedT<T, std::string>;
}
