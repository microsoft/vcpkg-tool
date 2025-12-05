#pragma once

#include <vcpkg/base/optional.h>

namespace vcpkg
{
    template<class T>
    struct Lazy
    {
        template<class F>
        T const& get_lazy(const F& f) const
        {
            if (auto existing = value.get())
            {
                return *existing;
            }

            return value.emplace(f());
        }

    private:
        mutable Optional<T> value;
    };
}
