#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/to-string.h>

#include <functional>
#include <system_error>
#include <type_traits>

namespace vcpkg
{
    struct Unit
    {
        // A meaningless type intended to be used with Expected when there is no meaningful value.
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
        ExpectedHolder() = delete;
        ExpectedHolder(const ExpectedHolder&) = default;
        ExpectedHolder(ExpectedHolder&&) = default;
        ExpectedHolder& operator=(const ExpectedHolder&) = default;
        ExpectedHolder& operator=(ExpectedHolder&&) = default;
        template<
            class Fwd,
            std::enable_if_t<!std::is_same_v<ExpectedHolder, std::remove_cv_t<std::remove_reference_t<Fwd>>>, int> = 0>
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
        ExpectedHolder(T& t) noexcept : t(&t) { }
        ExpectedHolder(const ExpectedHolder&) = default;
        ExpectedHolder& operator=(const ExpectedHolder&) = default;
        using pointer = T*;
        using const_pointer = T*;
        T* get() noexcept { return t; }
        T* get() const noexcept { return t; }
        T* t;
    };

    template<class T, class Error>
    struct ExpectedT
    {
        // Constructors are intentionally implicit

        // Each single argument ctor exists if we can convert to T or Error, and it isn't exactly the other type.
        // Note that this means both are effectively disabled when T == Error.
        template<class ConvToT,
                 std::enable_if_t<std::is_convertible_v<ConvToT, T> &&
                                      !std::is_same_v<std::remove_reference_t<ConvToT>, Error>,
                                  int> = 0>
        ExpectedT(ConvToT&& t) noexcept(std::is_nothrow_constructible_v<T, ConvToT>)
            : m_t(std::forward<ConvToT>(t)), value_is_error(false)
        {
        }

        template<class ConvToError,
                 std::enable_if_t<std::is_convertible_v<ConvToError, Error> &&
                                      !std::is_same_v<std::remove_reference_t<ConvToError>, T>,
                                  int> = 0,
                 int = 1>
        ExpectedT(ConvToError&& e) noexcept(std::is_nothrow_constructible_v<Error, ConvToError>)
            : m_error(std::forward<ConvToError>(e)), value_is_error(true)
        {
        }

        // Constructors that explicitly specify left or right exist if the parameter is convertible to T or Error
        template<class ConvToT, std::enable_if_t<std::is_convertible_v<ConvToT, T>, int> = 0>
        ExpectedT(ConvToT&& t, ExpectedLeftTag) noexcept(std::is_nothrow_constructible_v<T, ConvToT>)
            : m_t(std::forward<ConvToT>(t)), value_is_error(false)
        {
        }

        template<class ConvToError, std::enable_if_t<std::is_convertible_v<ConvToError, Error>, int> = 0>
        ExpectedT(ConvToError&& e, ExpectedRightTag) noexcept(std::is_nothrow_constructible_v<Error, ConvToError>)
            : m_error(std::forward<ConvToError>(e)), value_is_error(true)
        {
        }

        ExpectedT(const ExpectedT& other) noexcept(
            std::is_nothrow_copy_constructible_v<Error>&& std::is_nothrow_copy_constructible_v<ExpectedHolder<T>>)
            : value_is_error(other.value_is_error)
        {
            if (value_is_error)
            {
                ::new (&m_error) Error(other.m_error);
            }
            else
            {
                ::new (&m_t) ExpectedHolder<T>(other.m_t);
            }
        }

        ExpectedT(ExpectedT&& other) noexcept(
            std::is_nothrow_move_constructible_v<Error>&& std::is_nothrow_move_constructible_v<ExpectedHolder<T>>)
            : value_is_error(other.value_is_error)
        {
            if (value_is_error)
            {
                ::new (&m_error) Error(std::move(other.m_error));
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
                    m_error = std::move(other.m_error);
                }
                else
                {
                    m_error.~Error();
                    ::new (&m_t) ExpectedHolder<T>(std::move(other.m_t));
                    value_is_error = false;
                }
            }
            else
            {
                if (other.value_is_error)
                {
                    m_t.~ExpectedHolder<T>();
                    ::new (&m_error) Error(std::move(other.m_error));
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
                m_error.~Error();
            }
            else
            {
                m_t.~ExpectedHolder<T>();
            }
        }

        explicit constexpr operator bool() const noexcept { return !value_is_error; }
        constexpr bool has_value() const noexcept { return !value_is_error; }

        template<class... Args>
        T value_or(Args&&... or_args) &&
        {
            return !value_is_error ? std::move(*m_t.get()) : T(std::forward<Args>(or_args)...);
        }

        template<class... Args>
        T value_or(Args&&... or_args) const&
        {
            return !value_is_error ? *m_t.get() : T(std::forward<Args>(or_args)...);
        }

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

        const T& value(const LineInfo& line_info) const& noexcept
        {
            unreachable_if_error(line_info);
            return *m_t.get();
        }

        T&& value(const LineInfo& line_info) && noexcept
        {
            unreachable_if_error(line_info);
            return std::move(*m_t.get());
        }

        Error& error() & noexcept
        {
            unreachable_if_not_error(VCPKG_LINE_INFO);
            return m_error;
        }

        const Error& error() const& noexcept
        {
            unreachable_if_not_error(VCPKG_LINE_INFO);
            return m_error;
        }

        Error&& error() && noexcept
        {
            unreachable_if_not_error(VCPKG_LINE_INFO);
            return std::move(m_error);
        }

        const Error&& error() const&& noexcept
        {
            unreachable_if_not_error(VCPKG_LINE_INFO);
            return std::move(m_error);
        }

        typename ExpectedHolder<T>::const_pointer get() const noexcept
        {
            if (value_is_error)
            {
                return nullptr;
            }

            return m_t.get();
        }

        typename ExpectedHolder<T>::pointer get() noexcept
        {
            if (value_is_error)
            {
                return nullptr;
            }

            return m_t.get();
        }

        // map(F): returns an Expected<result_of_calling_F, Error>
        //
        // If *this holds a value, returns an expected holding the value F(*get())
        // Otherwise, returns an expected containing a copy of error()
        template<class F>
        ExpectedT<decltype(std::declval<F&>()(std::declval<const T&>())), Error> map(F f) const&
        {
            if (value_is_error)
            {
                return {m_error, expected_right_tag};
            }
            else
            {
                return {f(*m_t.get()), expected_left_tag};
            }
        }

        template<class F>
        ExpectedT<decltype(std::declval<F&>()(std::declval<T>())), Error> map(F f) &&
        {
            if (value_is_error)
            {
                return {std::move(m_error), expected_right_tag};
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
        ExpectedT<T, decltype(std::declval<F&>()(std::declval<const Error&>()))> map_error(F f) const&
        {
            if (value_is_error)
            {
                return {f(m_error), expected_right_tag};
            }
            else
            {
                return {*m_t.get(), expected_left_tag};
            }
        }

        template<class F>
        ExpectedT<T, decltype(std::declval<F&>()(std::declval<Error>()))> map_error(F f) &&
        {
            if (value_is_error)
            {
                return {f(std::move(m_error)), expected_right_tag};
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
        struct IsThenCompatibleExpected<ExpectedT<OtherTy, Error>> : std::true_type
        {
        };

    public:
        template<class F, class... Args>
        typename std::invoke_result<F, const T&, Args...>::type then(F f, Args&&... args) const&
        {
            static_assert(IsThenCompatibleExpected<typename std::invoke_result<F, const T&, Args...>::type>::value,
                          "then expects f to return an expected with the same error type");
            if (value_is_error)
            {
                return {m_error, expected_right_tag};
            }
            else
            {
                return std::invoke(f, *m_t.get(), static_cast<Args&&>(args)...);
            }
        }

        template<class F, class... Args>
        typename std::invoke_result<F, T, Args...>::type then(F f, Args&&... args) &&
        {
            static_assert(IsThenCompatibleExpected<typename std::invoke_result<F, T, Args...>::type>::value,
                          "then expects f to return an expected with the same error type");
            if (value_is_error)
            {
                return {m_error, expected_right_tag};
            }
            else
            {
                return std::invoke(f, std::move(*m_t.get()), static_cast<Args&&>(args)...);
            }
        }

    private:
        void exit_if_error(const LineInfo& line_info) const noexcept
        {
            if (value_is_error)
            {
                Checks::msg_exit_with_message(line_info, error());
            }
        }

        void unreachable_if_error(const LineInfo& line_info) const noexcept
        {
            if (value_is_error)
            {
                Checks::unreachable(line_info);
            }
        }

        void unreachable_if_not_error(const LineInfo& line_info) const noexcept
        {
            if (!value_is_error)
            {
                Checks::unreachable(line_info);
            }
        }

        union
        {
            Error m_error;
            ExpectedHolder<T> m_t;
        };

        bool value_is_error;
    };
}
