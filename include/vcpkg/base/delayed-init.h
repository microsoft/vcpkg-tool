#pragma once

#include <vcpkg/base/optional.h>

#include <memory>
#include <mutex>

namespace vcpkg
{
    constexpr struct EmptyDelayedInit
    {
    } delayed_init_empty;

    // implements the equivalent of function static initialization for an object
    template<class T>
    struct DelayedInit
    {
        DelayedInit() = default;
        DelayedInit(EmptyDelayedInit) { }

        template<class F>
        const T& get(F&& f) const
        {
            std::call_once(m_flag, [&f, this]() { m_storage.emplace(std::forward<F>(f)()); });
            return *m_storage.get();
        }

    private:
        mutable std::once_flag m_flag;
        mutable Optional<T> m_storage;
    };
}
