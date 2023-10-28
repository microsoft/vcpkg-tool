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
}
