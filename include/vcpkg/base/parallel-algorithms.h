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

    template<class T, class F>
    void parallel_for_each(View<T> view, F cb) noexcept
    {
        if (view.size() == 0)
        {
            return;
        }
        if (view.size() == 1)
        {
            cb(view[0]);
            return;
        }

        std::atomic_size_t next{0};

        execute_in_parallel(view.size(), [&]() {
            size_t i = 0;
            while (i < view.size())
            {
                if (next.compare_exchange_weak(i, i + 1, std::memory_order_relaxed))
                {
                    cb(view[i]);
                }
            }
        });
    }

    template<class T, class RanItTarget, class F>
    void parallel_transform(View<T> view, RanItTarget out_begin, F&& cb) noexcept
    {
        if (view.size() == 0)
        {
            return;
        }
        if (view.size() == 1)
        {
            *out_begin = cb(view[0]);
            return;
        }

        std::atomic_size_t next{0};

        execute_in_parallel(view.size(), [&]() {
            size_t i = 0;
            while (i < view.size())
            {
                if (next.compare_exchange_weak(i, i + 1, std::memory_order_relaxed))
                {
                    *(out_begin + i) = cb(view[i]);
                }
            }
        });
    }
}
