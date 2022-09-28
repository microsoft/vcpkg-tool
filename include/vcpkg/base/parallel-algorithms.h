#pragma once

// At this time only MSVC supports parallel algorithms by default
// Gcc needs tbb as a runtime dependency
// Clang doesn't support parallel algorithms at all

#if defined(_MSC_VER)
#include <algorithm>
#include <execution>
#include <mutex>

#define vcpkg_parallel_for_each(BEGIN, END, CB) std::for_each(std::execution::par, BEGIN, END, CB)
#define vcpkg_par_unseq_for_each(BEGIN, END, CB) std::for_each(std::execution::par_unseq, BEGIN, END, CB)

#else
#if 1
#include <vcpkg/base/checks.h>

#include <future>
#include <iostream>
#include <vector>

namespace vcpkg
{
    template<class It, class F>
    void parallel_for_each(It begin, It end, F cb)
    {
        if (begin == end)
        {
            return;
        }
        if (begin + 1 == end)
        {
            return cb(*begin);
        }

        const auto thread_count = std::thread::hardware_concurrency() * 2;
        const auto work_count = std::distance(begin, end);
        const auto num_threads = static_cast<size_t>(
            std::max(static_cast<ptrdiff_t>(1), std::min(static_cast<ptrdiff_t>(thread_count), work_count)));
        // How many items each thread should do; main thread does the remainder
        const auto [quot, rem] = std::div(work_count, num_threads);
        const auto _quot = quot; // Structured bindings can't be captured by lambdas
        const auto other_threads = rem == 0 ? num_threads - 1 : num_threads;

        std::vector<std::future<void>> workers;
        workers.reserve(other_threads);

        for (size_t i = 0; i < other_threads; ++i)
        {
            workers.emplace_back(std::async(std::launch::async, [&]() {
                for (size_t j = 0; j < static_cast<unsigned int>(std::abs(_quot)); ++j)
                {
                    cb(*(begin + i + j));
                }
            }));
        }

        for (It start = begin + other_threads * quot; start != end; ++start)
        {
            cb(*start);
        }

        for (auto&& w : workers)
        {
            w.get();
        }
    }
}

#define vcpkg_parallel_for_each(BEGIN, END, CB) ::vcpkg::parallel_for_each(BEGIN, END, CB)
#define vcpkg_par_unseq_for_each(BEGIN, END, CB) vcpkg_parallel_for_each(BEGIN, END, CB)
#else
#include <algorithm>
#define vcpkg_parallel_for_each(BEGIN, END, CB) std::for_each(BEGIN, END, CB)
#define vcpkg_par_unseq_for_each(BEGIN, END, CB) vcpkg_parallel_for_each(BEGIN, END, CB)
#endif
#endif
