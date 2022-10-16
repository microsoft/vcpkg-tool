#pragma once

namespace vcpkg
{
    template<class T>
    struct Lazy
    {
        Lazy() : value(T()) { }

        template<class F>
        T const& get_lazy(const F& f) const
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
        mutable bool initialized{false};
    };
}
