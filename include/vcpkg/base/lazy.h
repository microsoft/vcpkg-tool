#pragma once

#include <future>
#include <variant>

namespace vcpkg
{
    template<class T>
    struct Lazy
    {
        Lazy() : value(T()), initialized(false) { }

        template<class F>
        const T& get_lazy(const F& f) const
        {
            if (!initialized)
            {
                value = f();
                initialized = true;
            }
            return value;
        }

    private:
        mutable T value;
        mutable bool initialized;
    };

    template<class T>
    struct AsyncLazy
    {
        template<class F>
        AsyncLazy(F work) : value(std::async(std::launch::async | std::launch::deferred, [&work]() { return work(); }))
        {
        }

        const T& get() const
        {
            if (std::holds_alternative<std::future<T>>(value))
            {
                value = std::get<std::future<T>>(value).get();
            }
            return std::get<T>(value);
        }

    private:
        mutable std::variant<std::future<T>, T> value;
    };
}
