#pragma once

#include <map>
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

    template<class Key, class Value, class Compare = std::less<>>
    struct Cache
    {
        template<class KeyIsh,
                 class F,
                 std::enable_if_t<std::is_constructible<Key, const KeyIsh&>::value &&
                                      detail::is_callable<Compare&, const Key&, const KeyIsh&>::value,
                                  int> = 0>
        const Value& get_lazy(const KeyIsh& k, F&& f) const
        {
            auto it = m_cache.lower_bound(k);
            // lower_bound returns the first iterator such that it->first is greater than or equal to than k, so k must
            // be less than or equal to it->first. If k is not less than it->first, then it must be equal so we have a
            // cache hit.
            if (it != m_cache.end() && !(m_cache.key_comp()(k, it->first)))
            {
                return it->second;
            }

            return m_cache.emplace_hint(it, k, static_cast<F&&>(f)())->second;
        }

    private:
        mutable std::map<Key, Value, Compare> m_cache;
    };
}
