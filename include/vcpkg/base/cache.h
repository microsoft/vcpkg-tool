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
                 typename std::enable_if<std::is_constructible<Key, const KeyIsh&>::value &&
                                             detail::is_callable<Compare&, const Key&, const KeyIsh&>::value,
                                         int>::type = 0>
        const Value& get_lazy(const KeyIsh& k, F&& f) const
        {
            auto it = m_cache.find(k);
            if (it != m_cache.end()) return it->second;
            return m_cache.emplace(k, static_cast<F&&>(f)()).first->second;
        }

    private:
        mutable std::map<Key, Value, Compare> m_cache;
    };
}
