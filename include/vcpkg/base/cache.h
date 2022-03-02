#pragma once

#include <map>
#include <type_traits>
#include <utility>

namespace vcpkg
{
    template<typename Void, typename Callable, typename... Args>
    struct is_callable_impl : std::false_type
    {
    };

    template<typename Callable, typename... Args>
    struct is_callable_impl<std::void_t<decltype(std::declval<Callable>()(std::declval<Args>()...))>, Callable, Args...>
        : std::true_type
    {
    };

    template<typename Callable, typename... Args>
    using is_callable = is_callable_impl<void, Callable, Args...>;

    template<class Key, class Value, class Less = std::less<>>
    struct Cache
    {
        template<class KeyIsh,
                 class F,
                 typename std::enable_if<std::is_constructible<Key, const KeyIsh&>::value &&
                                             is_callable<Less&, const Key&, const KeyIsh&>::value,
                                         int>::type = 0>
        Value const& get_lazy(const KeyIsh& k, const F& f) const
        {
            auto it = m_cache.find(k);
            if (it != m_cache.end()) return it->second;
            return m_cache.emplace(k, f()).first->second;
        }

    private:
        mutable std::map<Key, Value, Less> m_cache;
    };
}
