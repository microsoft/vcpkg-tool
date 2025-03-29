#pragma once

#include <vcpkg/base/fwd/optional.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/pragmas.h>

#include <type_traits>
#include <utility>

namespace vcpkg
{
    struct NullOpt
    {
        explicit constexpr NullOpt(int) { }
    };

    const static constexpr NullOpt nullopt{0};

    template<class T>
    struct Optional;

    namespace details
    {
        struct EngageTag
        {
        };

        template<class T, bool IsTrivallyDestructible = std::is_trivially_destructible_v<T>>
        struct OptionalStorageDtor
        {
            bool m_is_present;
            union
            {
                char m_inactive;
                T m_t;
            };

            constexpr OptionalStorageDtor() : m_is_present(false), m_inactive() { }
            template<class... Args>
            constexpr OptionalStorageDtor(EngageTag,
                                          Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
                : m_is_present(true), m_t(std::forward<Args>(args)...)
            {
            }
        };

        template<class T>
        struct OptionalStorageDtor<T, false>
        {
            bool m_is_present;
            union
            {
                char m_inactive;
                T m_t;
            };

            constexpr OptionalStorageDtor() : m_is_present(false), m_inactive() { }
            template<class... Args>
            constexpr OptionalStorageDtor(EngageTag,
                                          Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
                : m_is_present(true), m_t(std::forward<Args>(args)...)
            {
            }

            ~OptionalStorageDtor()
            {
                if (m_is_present)
                {
                    m_t.~T();
                }
            }
        };

        template<class T, bool B = std::is_copy_constructible_v<T>>
        struct OptionalStorage : OptionalStorageDtor<T>
        {
            OptionalStorage() = default;
            constexpr OptionalStorage(const T& t) noexcept(std::is_nothrow_copy_constructible_v<T>)
                : OptionalStorageDtor<T>(EngageTag{}, t)
            {
            }
            constexpr OptionalStorage(T&& t) noexcept(std::is_nothrow_move_constructible_v<T>)
                : OptionalStorageDtor<T>(EngageTag{}, std::move(t))
            {
            }
            template<class U, class = std::enable_if_t<!std::is_reference_v<U>>>
            explicit OptionalStorage(Optional<U>&& t) noexcept(std::is_nothrow_constructible_v<T, U>)
                : OptionalStorageDtor<T>()
            {
                if (auto p = t.get())
                {
                    this->m_is_present = true;
                    new (&this->m_t) T(std::move(*p));
                }
            }
            template<class U>
            explicit OptionalStorage(const Optional<U>& t) noexcept(std::is_nothrow_constructible_v<T, const U&>)
                : OptionalStorageDtor<T>()
            {
                if (auto p = t.get())
                {
                    this->m_is_present = true;
                    new (&this->m_t) T(*p);
                }
            }

            OptionalStorage(const OptionalStorage& o) noexcept(std::is_nothrow_copy_constructible_v<T>)
                : OptionalStorageDtor<T>()
            {
                if (o.m_is_present)
                {
                    this->m_is_present = true;
                    new (&this->m_t) T(o.m_t);
                }
            }

            OptionalStorage(OptionalStorage&& o) noexcept(std::is_nothrow_move_constructible_v<T>)
                : OptionalStorageDtor<T>()
            {
                if (o.m_is_present)
                {
                    this->m_is_present = true;
                    new (&this->m_t) T(std::move(o.m_t));
                }
            }

            OptionalStorage& operator=(const OptionalStorage& o) noexcept(std::is_nothrow_copy_constructible_v<T> &&
                                                                          std::is_nothrow_copy_assignable_v<T>)
            {
                if (this->m_is_present && o.m_is_present)
                {
                    this->m_t = o.m_t;
                }
                else if (!this->m_is_present && o.m_is_present)
                {
                    new (&this->m_t) T(o.m_t);
                    this->m_is_present = true;
                }
                else if (this->m_is_present && !o.m_is_present)
                {
                    destroy();
                }

                return *this;
            }

            OptionalStorage& operator=(OptionalStorage&& o) noexcept // enforces termination
            {
                if (this->m_is_present && o.m_is_present)
                {
                    this->m_t = std::move(o.m_t);
                }
                else if (!this->m_is_present && o.m_is_present)
                {
                    new (&this->m_t) T(std::move(o.m_t));
                    this->m_is_present = true;
                }
                else if (this->m_is_present && !o.m_is_present)
                {
                    destroy();
                }
                return *this;
            }

            constexpr bool has_value() const noexcept { return this->m_is_present; }

            const T& value() const noexcept { return this->m_t; }
            T& value() noexcept { return this->m_t; }

            const T* get() const& noexcept { return this->m_is_present ? &this->m_t : nullptr; }
            T* get() & noexcept { return this->m_is_present ? &this->m_t : nullptr; }
            const T* get() const&& = delete;
            T* get() && = delete;

            void destroy() noexcept // enforces termination
            {
                this->m_is_present = false;
                this->m_t.~T();
                this->m_inactive = '\0';
            }

            template<class... Args>
            T& emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
            {
                if (this->m_is_present) destroy();
                new (&this->m_t) T(static_cast<Args&&>(args)...);
                this->m_is_present = true;
                return this->m_t;
            }
        };

        template<class T>
        struct OptionalStorage<T, false> : OptionalStorageDtor<T>
        {
            OptionalStorage() = default;
            constexpr OptionalStorage(T&& t) noexcept(std::is_nothrow_move_constructible_v<T>)
                : OptionalStorageDtor<T>(EngageTag{}, std::move(t))
            {
            }
            template<class U, class = std::enable_if_t<!std::is_reference_v<U>>>
            explicit OptionalStorage(Optional<U>&& t) noexcept(std::is_nothrow_constructible_v<T, U>)
                : OptionalStorageDtor<T>()
            {
                if (auto p = t.get())
                {
                    this->m_is_present = true;
                    new (&this->m_t) T(std::move(*p));
                }
            }
            OptionalStorage(OptionalStorage&& o) noexcept(std::is_nothrow_move_constructible_v<T>)
                : OptionalStorageDtor<T>()
            {
                if (o.m_is_present)
                {
                    this->m_is_present = true;
                    new (&this->m_t) T(std::move(o.m_t));
                }
            }

            OptionalStorage& operator=(OptionalStorage&& o) noexcept // enforces termination
            {
                if (this->m_is_present && o.m_is_present)
                {
                    this->m_t = std::move(o.m_t);
                }
                else if (!this->m_is_present && o.m_is_present)
                {
                    this->m_is_present = true;
                    new (&this->m_t) T(std::move(o.m_t));
                }
                else if (this->m_is_present && !o.m_is_present)
                {
                    destroy();
                }

                return *this;
            }

            constexpr bool has_value() const noexcept { return this->m_is_present; }

            const T& value() const noexcept { return this->m_t; }
            T& value() noexcept { return this->m_t; }

            const T* get() const& noexcept { return this->m_is_present ? &this->m_t : nullptr; }
            T* get() & noexcept { return this->m_is_present ? &this->m_t : nullptr; }
            const T* get() const&& = delete;
            T* get() && = delete;

            template<class... Args>
            T& emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
            {
                if (this->m_is_present) destroy();
                new (&this->m_t) T(static_cast<Args&&>(args)...);
                this->m_is_present = true;
                return this->m_t;
            }

            void destroy() noexcept
            {
                this->m_is_present = false;
                this->m_t.~T();
                this->m_inactive = '\0';
            }
        };

        template<class T, bool B>
        struct OptionalStorage<T&, B>
        {
            constexpr OptionalStorage() noexcept : m_t(nullptr) { }
            constexpr OptionalStorage(T& t) noexcept : m_t(&t) { }
            constexpr OptionalStorage(Optional<T>& t) noexcept : m_t(t.get()) { }

            constexpr bool has_value() const noexcept { return m_t != nullptr; }

            T& value() const noexcept { return *this->m_t; }

            T& emplace(T& t) noexcept
            {
                m_t = &t;
                return *m_t;
            }

            T* get() const noexcept { return m_t; }

            void destroy() noexcept { m_t = nullptr; }

        private:
            T* m_t;
        };

        template<class T, bool B>
        struct OptionalStorage<const T&, B>
        {
            constexpr OptionalStorage() noexcept : m_t(nullptr) { }
            constexpr OptionalStorage(const T& t) noexcept : m_t(&t) { }
            constexpr OptionalStorage(const Optional<T>& t) noexcept : m_t(t.get()) { }
            constexpr OptionalStorage(const Optional<const T>& t) noexcept : m_t(t.get()) { }
            constexpr OptionalStorage(Optional<T>&& t) = delete;
            constexpr OptionalStorage(Optional<const T>&& t) = delete;

            constexpr bool has_value() const noexcept { return m_t != nullptr; }

            const T& value() const noexcept { return *this->m_t; }

            const T* get() const noexcept { return m_t; }

            const T& emplace(const T& t) noexcept
            {
                m_t = &t;
                return *m_t;
            }

            void destroy() noexcept { m_t = nullptr; }

        private:
            const T* m_t;
        };
    }

    template<class T>
    struct Optional : private details::OptionalStorage<T>
    {
    public:
        constexpr Optional() noexcept { }

        // Constructors are intentionally implicit
        constexpr Optional(NullOpt) noexcept { }

        template<class U,
                 std::enable_if_t<!std::is_same_v<std::decay_t<U>, Optional> &&
                                      std::is_constructible_v<details::OptionalStorage<T>, U>,
                                  int> = 0>
        constexpr Optional(U&& t) noexcept(std::is_nothrow_constructible_v<details::OptionalStorage<T>, U>)
            : details::OptionalStorage<T>(static_cast<U&&>(t))
        {
        }

        using details::OptionalStorage<T>::emplace;
        using details::OptionalStorage<T>::has_value;
        using details::OptionalStorage<T>::get;

        T&& value_or_exit(const LineInfo& line_info) && noexcept
        {
            Checks::check_exit(line_info, this->has_value(), "Value was null");
            return std::move(this->value());
        }

        T& value_or_exit(const LineInfo& line_info) & noexcept
        {
            Checks::check_exit(line_info, this->has_value(), "Value was null");
            return this->value();
        }

        const T& value_or_exit(const LineInfo& line_info) const& noexcept
        {
            Checks::check_exit(line_info, this->has_value(), "Value was null");
            return this->value();
        }

        T&& value_or_quiet_exit(const LineInfo& line_info) && noexcept
        {
            if (!this->has_value())
            {
                Checks::exit_fail(line_info);
            }

            return std::move(this->value());
        }

        T& value_or_quiet_exit(const LineInfo& line_info) & noexcept
        {
            if (!this->has_value())
            {
                Checks::exit_fail(line_info);
            }

            return this->value();
        }

        const T& value_or_quiet_exit(const LineInfo& line_info) const& noexcept
        {
            if (!this->has_value())
            {
                Checks::exit_fail(line_info);
            }

            return this->value();
        }

        constexpr explicit operator bool() const noexcept { return this->has_value(); }

        template<class U>
        T value_or(U&& default_value) const&
        {
            return this->has_value() ? this->value() : static_cast<T>(std::forward<U>(default_value));
        }

        T value_or(T&& default_value) const&
        {
            return this->has_value() ? this->value() : static_cast<T&&>(default_value);
        }

        template<class U>
        T value_or(U&& default_value) &&
        {
            return this->has_value() ? std::move(this->value()) : static_cast<T>(std::forward<U>(default_value));
        }
        T value_or(T&& default_value) &&
        {
            return this->has_value() ? std::move(this->value()) : static_cast<T&&>(default_value);
        }

        template<class F>
        using map_t = decltype(std::declval<F&>()(std::declval<const T&>()));

        template<class F>
        Optional<map_t<F>> map(F f) const&
        {
            if (this->has_value())
            {
                return f(this->value());
            }
            return nullopt;
        }

        template<class F>
        map_t<F> then(F f) const&
        {
            if (this->has_value())
            {
                return f(this->value());
            }
            return nullopt;
        }

        template<class F>
        using move_map_t = decltype(std::declval<F&>()(std::declval<T&&>()));

        template<class F>
        Optional<move_map_t<F>> map(F f) &&
        {
            if (this->has_value())
            {
                return f(std::move(this->value()));
            }
            return nullopt;
        }

        template<class F>
        move_map_t<F> then(F f) &&
        {
            if (this->has_value())
            {
                return f(std::move(this->value()));
            }
            return nullopt;
        }

        void clear() noexcept
        {
            if (this->has_value())
            {
                this->destroy();
            }
        }

        friend bool operator==(const Optional& lhs, const Optional& rhs)
        {
            if (lhs.has_value())
            {
                if (rhs.has_value())
                {
                    return lhs.value() == rhs.value();
                }

                return false;
            }

            return !rhs.has_value();
        }
        friend bool operator!=(const Optional& lhs, const Optional& rhs) noexcept { return !(lhs == rhs); }
    };

    // these cannot be hidden friends, unfortunately
    template<class T, class U>
    auto operator==(const Optional<T>& lhs, const Optional<U>& rhs) -> decltype(*lhs.get() == *rhs.get())
    {
        if (lhs.has_value() && rhs.has_value())
        {
            return *lhs.get() == *rhs.get();
        }
        return lhs.has_value() == rhs.has_value();
    }
    template<class T, class U>
    auto operator==(const Optional<T>& lhs, const U& rhs) -> decltype(*lhs.get() == rhs)
    {
        return lhs.has_value() && *lhs.get() == rhs;
    }
    template<class T, class U>
    auto operator==(const T& lhs, const Optional<U>& rhs) -> decltype(lhs == *rhs.get())
    {
        return rhs.has_value() && lhs == *rhs.get();
    }

    template<class T, class U>
    auto operator!=(const Optional<T>& lhs, const Optional<U>& rhs) -> decltype(*lhs.get() != *rhs.get())
    {
        if (lhs.has_value() && rhs.has_value())
        {
            return *lhs.get() != *rhs.get();
        }
        return lhs.has_value() != rhs.has_value();
    }
    template<class T, class U>
    auto operator!=(const Optional<T>& lhs, const U& rhs) -> decltype(*lhs.get() != rhs)
    {
        return !lhs.has_value() || *lhs.get() != rhs;
    }
    template<class T, class U>
    auto operator!=(const T& lhs, const Optional<U>& rhs) -> decltype(lhs != *rhs.get())
    {
        return !rhs.has_value() || lhs != *rhs.get();
    }
}
