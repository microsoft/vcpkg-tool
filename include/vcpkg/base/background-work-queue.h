#pragma once

#include <condition_variable>
#include <mutex>
#include <vector>

template<class WorkItem>
struct BackgroundWorkQueue
{
    template<class... Args>
    void push(Args&&... args)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_tasks.emplace_back(std::forward<Args>(args)...);
        m_cv.notify_one();
    }

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

            m_cv.wait(lock);
        }
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_running = false;
        m_cv.notify_all();
    }

private:
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::vector<WorkItem> m_tasks;
    bool m_running = true;
};
