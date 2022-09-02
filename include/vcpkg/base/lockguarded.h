#pragma once

#include <vcpkg/base/fwd/lockguarded.h>

#include <mutex>

namespace vcpkg
{
    template<class T>
    struct LockGuarded
    {
        friend struct LockGuardPtr<T>;

    private:
        std::mutex m_mutex;
        T m_t;
    };

    template<class T>
    struct LockGuardPtr
    {
        T& operator*() { return m_ptr; }
        T* operator->() { return &m_ptr; }

        T* get() { return &m_ptr; }

        explicit LockGuardPtr(LockGuarded<T>& sync) : m_lock(sync.m_mutex), m_ptr(sync.m_t) { }

    private:
        std::lock_guard<std::mutex> m_lock;
        T& m_ptr;
    };
}
