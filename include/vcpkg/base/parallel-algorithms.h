#pragma once
#if defined(_WIN32)
#include <vcpkg/base/system-headers.h>
#else // ^^^ _WIN32 / !_WIN32 vvv
#include <vcpkg/base/optional.h>

#include <system_error>
#include <thread>
#include <type_traits>
#include <vector>
#endif // ^^^ _WIN32
#include <vcpkg/base/system.h>

#include <algorithm>
#include <atomic>

namespace vcpkg
{
    template<class F>
    struct WorkCallbackContext
    {
        F work;
        size_t work_count;
        std::atomic<size_t> next_offset;

        WorkCallbackContext(F init_f, size_t work_count) : work(init_f), work_count(work_count), next_offset(0) { }

        void run()
        {
            auto offset = next_offset.load(std::memory_order_relaxed);
            while (offset < work_count)
            {
                if (!next_offset.compare_exchange_weak(offset, offset + 1, std::memory_order_relaxed))
                {
                    continue;
                }

                work(offset);
            }
        }
    };

#if defined(_WIN32)
    struct PtpWork
    {
        PtpWork(_In_ PTP_WORK_CALLBACK pfnwk, _Inout_opt_ PVOID pv, _In_opt_ PTP_CALLBACK_ENVIRON pcbe)
            : ptp_work(CreateThreadpoolWork(pfnwk, pv, pcbe))
        {
        }
        PtpWork(const PtpWork&) = delete;
        PtpWork& operator=(const PtpWork&) = delete;
        ~PtpWork()
        {
            if (ptp_work)
            {
                ::WaitForThreadpoolWorkCallbacks(ptp_work, TRUE);
                ::CloseThreadpoolWork(ptp_work);
            }
        }

        explicit operator bool() { return ptp_work != nullptr; }

        void submit() { ::SubmitThreadpoolWork(ptp_work); }

    private:
        PTP_WORK ptp_work;
    };

    template<class F>
    inline void execute_in_parallel(size_t work_count, F work) noexcept
    {
        if (work_count == 0)
        {
            return;
        }

        if (work_count == 1)
        {
            work(size_t{});
        }

        WorkCallbackContext<F> context{work, work_count};
        PtpWork ptp_work([](PTP_CALLBACK_INSTANCE,
                            void* context,
                            PTP_WORK) noexcept { static_cast<WorkCallbackContext<F>*>(context)->run(); },
                         &context,
                         nullptr);
        if (ptp_work)
        {
            auto max_threads = (std::min)(work_count, static_cast<size_t>(get_concurrency()));
            // start at 1 to account for the running thread
            for (size_t i = 1; i < max_threads; ++i)
            {
                ptp_work.submit();
            }
        }

        context.run();
    }
#else  // ^^^ _WIN32 / !_WIN32 vvv
    struct JThread
    {
        template<class Arg0, std::enable_if_t<!std::is_same<JThread, std::decay_t<Arg0>>::value, int> = 0>
        JThread(Arg0&& arg0) : m_thread(std::forward<Arg0>(arg0))
        {
        }

        ~JThread() { m_thread.join(); }

        JThread(const JThread&) = delete;
        JThread& operator=(const JThread&) = delete;

    private:
        std::thread m_thread;
    };

    template<class F>
    inline void execute_in_parallel(size_t work_count, F work) noexcept
    {
        if (work_count == 0)
        {
            return;
        }

        if (work_count == 1)
        {
            work(size_t{});
            return;
        }

        WorkCallbackContext<F> context{work, work_count};
        const size_t bg_threads = std::min(work_count, static_cast<size_t>(get_concurrency())) - 1;
        std::vector<Optional<JThread>> workers(bg_threads);
        for (auto&& worker : workers)
        {
            try
            {
                worker.emplace([&]() { context.run(); });
            }
            catch (const std::system_error&)
            {
                // ok, just give up trying to create threads
                break;
            }
        }

        context.run();
        // destroying workers joins
    }
#endif // ^^^ !_WIN32

    template<class Container, class F>
    void parallel_for_each(Container&& c, F cb) noexcept
    {
        execute_in_parallel(c.size(), [&](size_t offset) { cb(c[offset]); });
    }

    template<class Container, class RanItTarget, class F>
    void parallel_transform(const Container& c, RanItTarget out_begin, F cb) noexcept
    {
        execute_in_parallel(c.size(), [&](size_t offset) { out_begin[offset] = cb(c[offset]); });
    }
}
