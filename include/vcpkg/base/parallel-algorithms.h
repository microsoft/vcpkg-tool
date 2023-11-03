#pragma once

#include <vcpkg/base/span.h>
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

    template<class Container, class F>
    void parallel_for_each(Container&& c, F cb) noexcept
    {
        if (c.size() == 0)
        {
            return;
        }
        if (c.size() == 1)
        {
            cb(c[0]);
            return;
        }

        std::atomic_size_t next{0};

        execute_in_parallel(c.size(), [&]() {
            size_t i = 0;
            while (i < c.size())
            {
                if (next.compare_exchange_weak(i, i + 1, std::memory_order_relaxed))
                {
                    cb(c[i]);
                }
            }
        });
    }

    template<class Container, class RanItTarget, class F>
    void parallel_transform(const Container& c, RanItTarget out_begin, F&& cb) noexcept
    {
        if (c.size() == 0)
        {
            return;
        }
        if (c.size() == 1)
        {
            *out_begin = cb(c[0]);
            return;
        }

        std::atomic_size_t next{0};

        execute_in_parallel(c.size(), [&]() {
            size_t i = 0;
            while (i < c.size())
            {
                if (next.compare_exchange_weak(i, i + 1, std::memory_order_relaxed))
                {
                    *(out_begin + i) = cb(c[i]);
                }
            }
        });
    }
}
