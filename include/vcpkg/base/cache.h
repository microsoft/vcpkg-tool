#pragma once

#include <vcpkg/base/delayed-init.h>

#include <map>
#include <mutex>
#include <type_traits>
#include <utility>

namespace vcpkg
{
    namespace detail
    {
        template<typename Void, typename Callable, typename... Args>
        struct is_callable_impl : std::false_type
        {
        };

        template<typename Callable, typename... Args>
        struct is_callable_impl<std::void_t<decltype(std::declval<Callable>()(std::declval<Args>()...))>,
                                Callable,
                                Args...> : std::true_type
        {
        };

        template<typename Callable, typename... Args>
        using is_callable = is_callable_impl<void, Callable, Args...>;
    }

    // Thread-safe Cache
    template<class Key, class Value, class Compare = std::less<>>
    struct Cache
    {
        // - It is safe to access entries from multiple threads
        // - It is safe to access independent keys from the same thread (nested)
        // - It is unsafe to recursively access the same key while initializing that key
        template<class KeyIsh,
                 class F,
                 std::enable_if_t<std::is_constructible_v<Key, const KeyIsh&> &&
                                      detail::is_callable<Compare&, const Key&, const KeyIsh&>::value,
                                  int> = 0>
        const Value& get_lazy(const KeyIsh& k, F&& f) const
        {
            return get_entry(k).get(f);
        }

    private:
        template<class KeyIsh>
        DelayedInit<Value>& get_entry(const KeyIsh& k) const
        {
            std::lock_guard<std::mutex> lk(m);
            auto it = m_cache.lower_bound(k);
            // lower_bound returns the first iterator such that it->first is greater than or equal to than k, so k must
            // be less than or equal to it->first. If k is not less than it->first, then it must be equal so we have a
            // cache hit.
            if (it != m_cache.end() && !(m_cache.key_comp()(k, it->first)))
            {
                return it->second;
            }

            return m_cache.emplace_hint(it, k, delayed_init_empty)->second;
        }

        mutable std::mutex m;
        mutable std::map<Key, DelayedInit<Value>, Compare> m_cache;
    };
}
