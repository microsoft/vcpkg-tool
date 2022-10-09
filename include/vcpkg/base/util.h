#pragma once

#include <vcpkg/base/optional.h>
#include <vcpkg/base/view.h>

#include <algorithm>
#include <functional>
#include <map>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vcpkg::Util
{
    template<class Container>
    using ElementT =
        std::remove_reference_t<decltype(*std::declval<typename std::remove_reference_t<Container>::iterator>())>;

    namespace Vectors
    {
        template<class Container, class T = ElementT<Container>>
        void append(std::vector<T>* augend, const Container& addend)
        {
            augend->insert(augend->end(), addend.begin(), addend.end());
        }
        template<class Vec, class Key>
        bool contains(const Vec& container, const Key& item)
        {
            return std::find(std::begin(container), std::end(container), item) != std::end(container);
        }
        template<class T>
        std::vector<T> concat(View<T> r1, View<T> r2)
        {
            std::vector<T> v;
            v.reserve(r1.size() + r2.size());
            v.insert(v.end(), r1.begin(), r1.end());
            v.insert(v.end(), r2.begin(), r2.end());
            return v;
        }
    }

    namespace Sets
    {
        template<class Container, class Key>
        bool contains(const Container& container, const Key& item)
        {
            return container.find(item) != container.end();
        }
    }

    namespace Maps
    {
        template<class K, class V1, class V2, class Func>
        void transform_values(const std::unordered_map<K, V1>& container, std::unordered_map<K, V2>& output, Func func)
        {
            std::for_each(container.cbegin(), container.cend(), [&](const std::pair<const K, V1>& p) {
                output.emplace(
                    std::piecewise_construct, std::forward_as_tuple(p.first), std::forward_as_tuple(func(p.second)));
            });
        }
    }

    template<class Range, class Pred, class E = ElementT<Range>>
    std::vector<E> filter(const Range& xs, Pred&& f)
    {
        std::vector<E> ret;

        for (auto&& x : xs)
        {
            if (f(x))
            {
                ret.emplace_back(x);
            }
        }

        return ret;
    }

    template<class Range, class Func>
    using FmapRefOut = decltype(std::declval<Func&>()(*std::declval<Range>().begin()));

    template<class Range, class Func>
    using FmapOut = std::decay_t<FmapRefOut<Range, Func>>;

    template<class Range, class Func>
    std::vector<FmapOut<Range, Func>> fmap(Range&& xs, Func&& f)
    {
        std::vector<FmapOut<Range, Func>> ret;
        ret.reserve(xs.size());

        for (auto&& x : xs)
        {
            ret.emplace_back(f(x));
        }

        return ret;
    }

    template<class Range, class Proj, class Out = FmapRefOut<Range, Proj>>
    Optional<Out> common_projection(Range&& input, Proj&& proj)
    {
        const auto last = input.end();
        auto first = input.begin();
        if (first == last)
        {
            return nullopt;
        }

        Out prototype = proj(*first);
        while (++first != last)
        {
            if (prototype != proj(*first))
            {
                return nullopt;
            }
        }

        return prototype;
    }

    template<class Cont, class Func>
    using FmapFlattenOut = std::decay_t<decltype(*begin(std::declval<Func>()(*begin(std::declval<Cont>()))))>;

    template<class Cont, class Func, class Out = FmapFlattenOut<Cont, Func>>
    std::vector<Out> fmap_flatten(Cont&& xs, Func&& f)
    {
        std::vector<Out> ret;
        ret.reserve(xs.size());

        for (auto&& x : xs)
        {
            for (auto&& y : f(x))
            {
                ret.push_back(std::move(y));
            }
        }

        return ret;
    }

    template<class Container, class T>
    void erase_remove(Container& cont, const T& el)
    {
        cont.erase(std::remove(cont.begin(), cont.end(), el), cont.end());
    }
    template<class Container, class Pred>
    void erase_remove_if(Container& cont, Pred pred)
    {
        cont.erase(std::remove_if(cont.begin(), cont.end(), pred), cont.end());
    }

    template<class Range, class F>
    void transform(Range& r, F f)
    {
        std::transform(r.begin(), r.end(), r.begin(), f);
    }

    template<class ForwardIt1, class ForwardIt2>
    ForwardIt1 search_and_skip(ForwardIt1 first, ForwardIt1 last, ForwardIt2 s_first, ForwardIt2 s_last)
    {
        first = std::search(first, last, s_first, s_last);
        if (first != last)
        {
            std::advance(first, std::distance(s_first, s_last));
        }
        return first;
    }

    template<class ForwardIt1, class ForwardRange2>
    ForwardIt1 search_and_skip(ForwardIt1 first, ForwardIt1 last, const ForwardRange2& rng)
    {
        return ::vcpkg::Util::search_and_skip(first, last, std::begin(rng), std::end(rng));
    }

    template<class ForwardIt1, class ForwardRange2>
    ForwardIt1 search(ForwardIt1 first, ForwardIt1 last, const ForwardRange2& rng)
    {
        return std::search(first, last, std::begin(rng), std::end(rng));
    }

    // 0th is the first occurence
    // so find_nth({1, 2, 1, 3, 1, 4}, 1, 2)
    // returns the 1 before the 4
    template<class InputIt, class V>
    auto find_nth(InputIt first, InputIt last, const V& v, size_t n)
    {
        first = std::find(first, last, v);
        for (size_t i = 0; i < n && first != last; ++i)
        {
            ++first;
            first = std::find(first, last, v);
        }

        return first;
    }
    template<class R, class V>
    auto find_nth(R& r, const V& v, size_t n)
    {
        return find_nth(std::begin(r), std::end(r), v, n);
    }

    // 0th is the last occurence
    // so find_nth({1, 2, 1, 3, 1, 4}, 1, 2)
    // returns the 1 before the 2
    template<class R, class V>
    auto find_nth_from_last(R& r, const V& v, size_t n)
    {
        auto res = find_nth(std::rbegin(r), std::rend(r), v, n);
        return res == std::rend(r) ? std::end(r) : res.base() - 1;
    }

    template<class Container, class V>
    auto find(Container&& cont, V&& v)
    {
        return std::find(std::begin(cont), std::end(cont), v);
    }

    template<class Container, class Pred>
    auto find_if(Container&& cont, Pred pred)
    {
        // allow cont.begin() to not have the same type as cont.end()
        auto it = std::begin(cont);
        auto last = std::end(cont);
        for (; it != last; ++it)
        {
            if (pred(*it))
            {
                break;
            }
        }
        return it;
    }

    template<class Container, class Pred>
    auto find_if_not(Container&& cont, Pred pred)
    {
        return std::find_if_not(std::begin(cont), std::end(cont), pred);
    }

    template<class K, class V, class Container, class Func>
    void group_by(const Container& cont, std::map<K, std::vector<const V*>>* output, Func&& f)
    {
        for (const V& element : cont)
        {
            K key = f(element);
            (*output)[key].push_back(&element);
        }
    }

    template<class Range, class Comp = std::less<>>
    void sort(Range& cont, Comp comp = Comp())
    {
        std::sort(std::begin(cont), std::end(cont), comp);
    }

    template<class Range, class Pred>
    bool any_of(Range&& rng, Pred pred)
    {
        return std::any_of(rng.begin(), rng.end(), std::move(pred));
    }

    template<class Range, class Comp = std::less<>>
    Range&& sort_unique_erase(Range&& cont, Comp comp = Comp())
    {
        std::sort(std::begin(cont), std::end(cont), comp);
        cont.erase(std::unique(std::begin(cont), std::end(cont)), std::end(cont));

        return std::forward<Range>(cont);
    }

    template<class Range1, class Range2>
    bool all_equal(const Range1& r1, const Range2& r2)
    {
        return std::equal(std::begin(r1), std::end(r1), std::begin(r2), std::end(r2));
    }

    template<class AssocContainer, class K = std::decay_t<decltype(begin(std::declval<AssocContainer>())->first)>>
    std::vector<K> extract_keys(AssocContainer&& input_map)
    {
        return fmap(input_map, [](auto&& p) { return p.first; });
    }

    template<class Range1, class Range2, class Comp>
    int range_lexcomp(const Range1& r1, const Range2& r2, Comp cmp)
    {
        static_assert(std::is_same_v<int, decltype(cmp(*begin(r1), *std::begin(r2)))>,
                      "Comp must return 'int' (negative for less, 0 for equal, positive for greater)");

        auto a_cur = std::begin(r1);
        auto a_end = std::end(r1);

        auto b_cur = std::begin(r2);
        auto b_end = std::end(r2);

        for (; a_cur != a_end && b_cur != b_end; ++a_cur, ++b_cur)
        {
            int x = cmp(*a_cur, *b_cur);
            if (x != 0)
            {
                return x;
            }
        }
        // 1 - 0 => a is larger
        // 0 - 1 => b is larger
        // 1 - 1 => equal
        return (b_cur == b_end) - (a_cur == a_end);
    }

    template<class... BoolIsh>
    constexpr bool zero_or_one_set(const BoolIsh&... boolish)
    {
        unsigned int total = (0u + ... + static_cast<unsigned int>(static_cast<bool>(boolish)));
        return total <= 1;
    }

    namespace Enum
    {
        template<class E>
        constexpr E to_enum(bool b) noexcept
        {
            return b ? E::YES : E::NO;
        }

        template<class E>
        constexpr bool to_bool(E e) noexcept
        {
            return e == E::YES;
        }
    }
}
