#pragma once

#include <vcpkg/base/system.h>

#include <atomic>
#include <future>
#include <vector>

namespace vcpkg
{
    template<class It, class F>
    void parallel_for_each(It begin, size_t work_count, F&& cb)
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

        const size_t thread_count = static_cast<size_t>(get_concurrency());
        const size_t num_threads = std::max(static_cast<size_t>(1), std::min(thread_count, work_count));

        std::vector<std::future<void>> workers;
        workers.reserve(num_threads - 1);

        std::atomic_size_t next{0};
        auto work = [&]() {
            size_t i;
            while (i = next.fetch_add(1, std::memory_order_relaxed), i < work_count)
            {
                cb(*(begin + i));
            }
        };

        for (size_t i = 0; i < num_threads - 1; ++i)
        {
            workers.emplace_back(std::async(std::launch::async, [&work]() { work(); }));
        }
        work();

        for (auto&& w : workers)
        {
            w.get();
        }
    }
}
