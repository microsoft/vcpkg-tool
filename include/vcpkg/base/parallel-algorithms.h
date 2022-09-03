#pragma once

#include <algorithm>

#if defined(USE_PARALLEL_ALG)
#include <execution>
#endif

namespace vcpkg
{
    template<typename Iter, typename F>
    void parallel_for_each(Iter begin, Iter end, F cb)
    {
#if defined(USE_PARALLEL_ALG)
        std::for_each(std::execution::par, begin, end, cb);
#else
        std::for_each(begin, end, cb);
#endif
    }
}
