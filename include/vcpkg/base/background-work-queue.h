#pragma once

#include <stddef.h>

#include <condition_variable>
#include <mutex>
#include <vector>

template<class WorkItem>
struct BackgroundWorkQueue
{
    // any thread can call push to add work to the queue
    template<class... Args>
    void push(Args&&... args)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_tasks.emplace_back(std::forward<Args>(args)...);
        m_cv.notify_one();
    }

    // at most one background worker thread calls get_work to fetch from the queue
    bool get_work(std::vector<WorkItem>& out)
    {
        std::unique_lock<std::mutex> lock(m_mtx);
        for (;;)
        {
            if (!m_tasks.empty())
            {
                out.clear();
                swap(out, m_tasks);
                return true;
            }

            if (!m_running)
            {
                return false;
            }

            // The notify_alls under lock could technically be done out of lock as long as modifications of m_quiescent
            // are under lock. However, this is simpler, and with only 2 threads in practice it seems like extra atomic
            // ops to update the mutex state would be worse than just doing the notify under lock too
            m_quiescent = true;
            m_cv.notify_all();
            m_cv.wait(lock);
            m_quiescent = false;
            m_cv.notify_all();
        }
    }

    // wait until the background thread is waiting for more work
    void wait_quiescent() const
    {
        std::unique_lock<std::mutex> lock(m_mtx);
        m_cv.wait(lock, [this] { return m_quiescent; });
    }

    // any thread can call stop to signal the background worker thread to stop after all the work currently in the queue
    // is done
    void stop()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_running = false;
        m_cv.notify_all();
    }

private:
    mutable std::mutex m_mtx;
    mutable std::condition_variable m_cv;
    std::vector<WorkItem> m_tasks;
    bool m_running = true;
    bool m_quiescent = false;
};
