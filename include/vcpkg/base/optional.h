#pragma once

#include <vcpkg/base/fwd/optional.h>

#include <vcpkg/base/basic_checks.h>
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
        template<class T, bool B = std::is_copy_constructible<T>::value>
        struct OptionalStorage
        {
            constexpr OptionalStorage() noexcept : m_is_present(false), m_inactive() { }
            constexpr OptionalStorage(const T& t) : m_is_present(true), m_t(t) { }
            constexpr OptionalStorage(T&& t) : m_is_present(true), m_t(std::move(t)) { }
            template<class U, class = std::enable_if_t<!std::is_reference<U>::value>>
            explicit OptionalStorage(Optional<U>&& t) : m_is_present(false), m_inactive()
            {
                if (auto p = t.get())
                {
                    m_is_present = true;
                    new (&m_t) T(std::move(*p));
                }
            }
            template<class U>
            explicit OptionalStorage(const Optional<U>& t) : m_is_present(false), m_inactive()
            {
                if (auto p = t.get())
                {
                    m_is_present = true;
                    new (&m_t) T(*p);
                }
            }

            ~OptionalStorage() noexcept
            {
                if (m_is_present) m_t.~T();
            }

            OptionalStorage(const OptionalStorage& o) : m_is_present(o.m_is_present), m_inactive()
            {
                if (m_is_present) new (&m_t) T(o.m_t);
            }

            OptionalStorage(OptionalStorage&& o) noexcept : m_is_present(o.m_is_present), m_inactive()
            {
                if (m_is_present)
                {
                    new (&m_t) T(std::move(o.m_t));
                }
            }

            OptionalStorage& operator=(const OptionalStorage& o)
            {
                if (m_is_present && o.m_is_present)
                {
                    m_t = o.m_t;
                }
                else if (!m_is_present && o.m_is_present)
                {
                    m_is_present = true;
                    new (&m_t) T(o.m_t);
                }
                else if (m_is_present && !o.m_is_present)
                {
                    destroy();
                }
                return *this;
            }

            OptionalStorage& operator=(OptionalStorage&& o) noexcept
            {
                if (m_is_present && o.m_is_present)
                {
                    m_t = std::move(o.m_t);
                }
                else if (!m_is_present && o.m_is_present)
                {
                    m_is_present = true;
                    new (&m_t) T(std::move(o.m_t));
                }
                else if (m_is_present && !o.m_is_present)
                {
                    destroy();
                }
                return *this;
            }

            constexpr bool has_value() const { return m_is_present; }

            const T& value() const { return this->m_t; }
            T& value() { return this->m_t; }

            void destroy()
            {
                m_is_present = false;
                m_t.~T();
                m_inactive = '\0';
            }

            template<class... Args>
            T& emplace(Args&&... args)
            {
                if (m_is_present)
                {
                    m_t = T(static_cast<Args&&>(args)...);
                }
                else
                {
                    new (&m_t) T(static_cast<Args&&>(args)...);
                    m_is_present = true;
                }
                return m_t;
            }

        private:
            bool m_is_present;
            union
            {
                char m_inactive;
                T m_t;
            };
        };

        template<class T>
        struct OptionalStorage<T, false>
        {
            constexpr OptionalStorage() noexcept : m_is_present(false), m_inactive() { }
            constexpr OptionalStorage(T&& t) : m_is_present(true), m_t(std::move(t)) { }

            ~OptionalStorage() noexcept
            {
                if (m_is_present) m_t.~T();
            }

            OptionalStorage(OptionalStorage&& o) noexcept : m_is_present(o.m_is_present), m_inactive()
            {
                if (m_is_present)
                {
                    new (&m_t) T(std::move(o.m_t));
                }
            }

            OptionalStorage& operator=(OptionalStorage&& o) noexcept
            {
                if (m_is_present && o.m_is_present)
                {
                    m_t = std::move(o.m_t);
                }
                else if (!m_is_present && o.m_is_present)
                {
                    m_is_present = true;
                    new (&m_t) T(std::move(o.m_t));
                }
                else if (m_is_present && !o.m_is_present)
                {
                    destroy();
                }
                return *this;
            }

            constexpr bool has_value() const { return m_is_present; }

            const T& value() const { return this->m_t; }
            T& value() { return this->m_t; }

            template<class... Args>
            T& emplace(Args&&... args)
            {
                if (m_is_present)
                {
                    m_t = T(static_cast<Args&&>(args)...);
                }
                else
                {
                    new (&m_t) T(static_cast<Args&&>(args)...);
                    m_is_present = true;
                }
                return m_t;
            }

            void destroy()
            {
                m_is_present = false;
                m_t.~T();
                m_inactive = '\0';
            }

        private:
            bool m_is_present;
            union
            {
                char m_inactive;
                T m_t;
            };
        };

        template<class T, bool B>
        struct OptionalStorage<T&, B>
        {
            constexpr OptionalStorage() noexcept : m_t(nullptr) { }
            constexpr OptionalStorage(T& t) : m_t(&t) { }
            constexpr OptionalStorage(Optional<T>& t) : m_t(t.get()) { }

            constexpr bool has_value() const { return m_t != nullptr; }

            T& value() const { return *this->m_t; }

            T& emplace(T& t)
            {
                m_t = &t;
                return *m_t;
            }

        private:
            T* m_t;
        };

        template<class T, bool B>
        struct OptionalStorage<const T&, B>
        {
            constexpr OptionalStorage() noexcept : m_t(nullptr) { }
            constexpr OptionalStorage(const T& t) : m_t(&t) { }
            constexpr OptionalStorage(const Optional<T>& t) : m_t(t.get()) { }
            constexpr OptionalStorage(const Optional<const T>& t) : m_t(t.get()) { }
            constexpr OptionalStorage(Optional<T>&& t) = delete;
            constexpr OptionalStorage(Optional<const T>&& t) = delete;

            constexpr bool has_value() const { return m_t != nullptr; }

            const T& value() const { return *this->m_t; }

            const T& emplace(const T& t)
            {
                m_t = &t;
                return *m_t;
            }

        private:
            const T* m_t;
        };
    }

    template<class T>
    struct Optional
    {
        constexpr Optional() noexcept { }

        // Constructors are intentionally implicit
        constexpr Optional(NullOpt) { }

        template<class U, class = std::enable_if_t<!std::is_same<std::decay_t<U>, Optional>::value>>
        constexpr Optional(U&& t) : m_base(std::forward<U>(t))
        {
        }

        T&& value_or_exit(const LineInfo& line_info) &&
        {
            Checks::check_exit(line_info, this->m_base.has_value(), "Value was null");
            return std::move(this->m_base.value());
        }

        T& value_or_exit(const LineInfo& line_info) &
        {
            Checks::check_exit(line_info, this->m_base.has_value(), "Value was null");
            return this->m_base.value();
        }

        const T& value_or_exit(const LineInfo& line_info) const&
        {
            Checks::check_exit(line_info, this->m_base.has_value(), "Value was null");
            return this->m_base.value();
        }

        constexpr explicit operator bool() const { return this->m_base.has_value(); }

        template<class... Args>
        T& emplace(Args&&... args)
        {
            return this->m_base.emplace(static_cast<Args&&>(args)...);
        }

        constexpr bool has_value() const { return this->m_base.has_value(); }

        template<class U>
        T value_or(U&& default_value) const&
        {
            return this->m_base.has_value() ? this->m_base.value() : static_cast<T>(std::forward<U>(default_value));
        }

        T value_or(T&& default_value) const&
        {
            return this->m_base.has_value() ? this->m_base.value() : static_cast<T&&>(default_value);
        }

        template<class U>
        T value_or(U&& default_value) &&
        {
            return this->m_base.has_value() ? std::move(this->m_base.value())
                                            : static_cast<T>(std::forward<U>(default_value));
        }
        T value_or(T&& default_value) &&
        {
            return this->m_base.has_value() ? std::move(this->m_base.value()) : static_cast<T&&>(default_value);
        }

        typename std::add_pointer<const T>::type get() const
        {
            return this->m_base.has_value() ? &this->m_base.value() : nullptr;
        }

        typename std::add_pointer<T>::type get() { return this->m_base.has_value() ? &this->m_base.value() : nullptr; }

        template<class F>
        using map_t = decltype(std::declval<F&>()(std::declval<const T&>()));

        template<class F, class U = map_t<F>>
        Optional<U> map(F f) const&
        {
            if (this->m_base.has_value())
            {
                return f(this->m_base.value());
            }
            return nullopt;
        }

        template<class F, class U = map_t<F>>
        U then(F f) const&
        {
            if (this->m_base.has_value())
            {
                return f(this->m_base.value());
            }
            return nullopt;
        }

        template<class F>
        using move_map_t = decltype(std::declval<F&>()(std::declval<T&&>()));

        template<class F, class U = move_map_t<F>>
        Optional<U> map(F f) &&
        {
            if (this->m_base.has_value())
            {
                return f(std::move(this->m_base.value()));
            }
            return nullopt;
        }

        template<class F, class U = move_map_t<F>>
        U then(F f) &&
        {
            if (this->m_base.has_value())
            {
                return f(std::move(this->m_base.value()));
            }
            return nullopt;
        }

        void clear()
        {
            if (this->m_base.has_value())
            {
                this->m_base.destroy();
            }
        }

        friend bool operator==(const Optional& lhs, const Optional& rhs)
        {
            if (lhs.m_base.has_value())
            {
                if (rhs.m_base.has_value())
                {
                    return lhs.m_base.value() == rhs.m_base.value();
                }

                return false;
            }

            return !rhs.m_base.has_value();
        }
        friend bool operator!=(const Optional& lhs, const Optional& rhs) noexcept { return !(lhs == rhs); }

    private:
        details::OptionalStorage<T> m_base;
    };

    template<class U>
    Optional<std::decay_t<U>> make_optional(U&& u)
    {
        return Optional<std::decay_t<U>>(std::forward<U>(u));
    }

    template<class T>
    bool operator==(const Optional<T>& o, const T& t)
    {
        if (auto p = o.get()) return *p == t;
        return false;
    }
    template<class T>
    bool operator==(const T& t, const Optional<T>& o)
    {
        if (auto p = o.get()) return t == *p;
        return false;
    }
    template<class T>
    bool operator!=(const Optional<T>& o, const T& t)
    {
        if (auto p = o.get()) return *p != t;
        return true;
    }
    template<class T>
    bool operator!=(const T& t, const Optional<T>& o)
    {
        if (auto p = o.get()) return t != *p;
        return true;
    }
}
