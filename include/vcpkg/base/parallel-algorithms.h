#pragma once

#include <vcpkg/base/system.h>

#include <atomic>
#include <future>
#include <vector>

namespace vcpkg
{
    template<class F>
    inline void execute_in_parallel(size_t work_count, F&& work) noexcept
    {
        const size_t thread_count = static_cast<size_t>(get_concurrency());
        const size_t num_threads = std::max(static_cast<size_t>(1), std::min(thread_count, work_count));

        std::vector<std::future<void>> workers;
        workers.reserve(num_threads - 1);

        for (size_t i = 0; i < num_threads - 1; ++i)
        {
            workers.emplace_back(std::async(std::launch::async | std::launch::deferred, [&work]() { work(); }));
        }
        work();

        for (auto&& w : workers)
        {
            w.get();
        }
    }

    template<class RanIt, class F>
    void parallel_for_each_n(RanIt begin, size_t work_count, F cb) noexcept
    {
        if (work_count == 0)
        {
            return;
        }
        if (work_count == 1)
        {
            cb(*begin);
            return;
        }

        std::atomic_size_t next{0};

        execute_in_parallel(work_count, [&]() {
            size_t i = 0;
            while (i < work_count)
            {
                if (next.compare_exchange_weak(i, i + 1, std::memory_order_relaxed))
                {
                    cb(*(begin + i));
                }
            }
        });
    }

    template<class RanItSource, class RanItTarget, class F>
    void parallel_transform(RanItSource begin, size_t work_count, RanItTarget out_begin, F&& cb) noexcept
    {
        if (work_count == 0)
        {
            return;
        }
        if (work_count == 1)
        {
            *out_begin = cb(*begin);
            return;
        }

        std::atomic_size_t next{0};

        execute_in_parallel(work_count, [&]() {
            size_t i = 0;
            while (i < work_count)
            {
                if (next.compare_exchange_weak(i, i + 1, std::memory_order_relaxed))
                {
                    *(out_begin + i) = cb(*(begin + i));
                }
            }
        });
    }
}
