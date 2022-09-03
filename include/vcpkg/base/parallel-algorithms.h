#pragma once

#if defined(USE_PARALLEL_ALG)
#include <algorithm>
#include <execution>
#elif defined(GNU_USE_PARALLEL_ALG)
#include <parallel/algorithm>
#else
#include <algorithm>
#endif

namespace vcpkg
{
    template<typename Iter, typename F>
    void parallel_for_each(Iter begin, Iter end, F cb)
    {
#if defined(USE_PARALLEL_ALG)
        std::for_each(std::execution::par, begin, end, cb);
#elif defined(GNU_USE_PARALLEL_ALG)
        __gnu_parallel::for_each(begin, end, cb);
#else
        std::for_each(begin, end, cb);
#endif
    }
}
