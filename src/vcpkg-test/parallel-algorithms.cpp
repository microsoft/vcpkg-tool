#include <vcpkg-test/util.h>

#include <vcpkg/base/parallel-algorithms.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace vcpkg;

TEST_CASE ("execute_in_parallel respects max concurrency", "[parallel-algorithms]")
{
    std::atomic<size_t> active_work{0};
    std::atomic<size_t> maximum_active_work{0};
    std::atomic<size_t> completed_work{0};

    execute_in_parallel(100, 2, [&](size_t) {
        const auto active = active_work.fetch_add(1, std::memory_order_relaxed) + 1;
        auto observed_maximum = maximum_active_work.load(std::memory_order_relaxed);
        while (observed_maximum < active &&
               !maximum_active_work.compare_exchange_weak(observed_maximum, active, std::memory_order_relaxed))
        {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        active_work.fetch_sub(1, std::memory_order_relaxed);
        completed_work.fetch_add(1, std::memory_order_relaxed);
    });

    CHECK(completed_work == 100);
    CHECK(maximum_active_work >= 1);
    CHECK(maximum_active_work <= 2);
}
