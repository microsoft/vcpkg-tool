#pragma once

#include <vcpkg/base/optional.h>
#include <vcpkg/base/span.h>

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
    using ElementT = std::decay_t<decltype(*std::declval<Container&>().begin())>;

    namespace Vectors
    {
        template<class Container, class T>
        void append(std::vector<T>* augend, Container&& addend)
        {
            if constexpr (std::is_lvalue_reference_v<Container> || std::is_const_v<Container>)
            {
                augend->insert(augend->end(), addend.begin(), addend.end());
            }
            else
            {
                augend->insert(
                    augend->end(), std::make_move_iterator(addend.begin()), std::make_move_iterator(addend.end()));
            }
        }
        template<class Vec, class Key, class KeyEqual>
        bool contains(const Vec& container, const Key& item, KeyEqual key_equal)
        {
            for (auto&& citem : container)
            {
                if (key_equal(citem, item))
                {
                    return true;
                }
            }

            return false;
        }
        template<class Vec, class Filter>
        std::vector<ElementT<Vec>> filtered_copy(const Vec& container, const Filter&& filter)
        {
            std::vector<ElementT<Vec>> ret;
            for (auto&& item : container)
            {
                if (filter(item))
                {
                    ret.push_back(item);
                }
            }
            return ret;
        }
        template<class Vec, class Key>
        bool contains(const Vec& container, const Key& item)
        {
            return std::find(container.begin(), container.end(), item) != container.end();
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

    // Treats the range [first, last) a range sorted by cmp, and copies any duplicate elements to
    // the output range out.
    template<class FwdIt, class OutIt, class Cmp>
    void set_duplicates(FwdIt first, FwdIt last, OutIt out, Cmp cmp)
    {
        // pre: [first, last) is sorted according to cmp
        if (first != last)
        {
            for (auto next = first; ++next != last; first = next)
            {
                if (!(cmp(*first, *next)))
                {
                    *out = *first;
                    ++out;
                    do
                    {
                        ++next;
                        if (next == last)
                        {
                            return;
                        }
                    } while (!(cmp(*first, *next)));
                }
            }
        }
    }

    template<class FwdIt, class OutIt>
    void set_duplicates(FwdIt first, FwdIt last, OutIt out)
    {
        return set_duplicates(first, last, out, std::less<>{});
    }

    namespace Maps
    {
        template<class Container, class Key>
        bool contains(const Container& container, Key&& item)
        {
            return container.find(static_cast<Key&&>(item)) != container.end();
        }

        template<class K, class V1, class V2, class Func>
        void transform_values(const std::unordered_map<K, V1>& container, std::unordered_map<K, V2>& output, Func func)
        {
            std::for_each(container.cbegin(), container.cend(), [&](const std::pair<const K, V1>& p) {
                output.emplace(
                    std::piecewise_construct, std::forward_as_tuple(p.first), std::forward_as_tuple(func(p.second)));
            });
        }
    }

    template<class Map, class Key>
    typename Map::mapped_type copy_or_default(const Map& map, Key&& key)
    {
        const auto it = map.find(static_cast<Key&&>(key));
        if (it == map.end())
        {
            return typename Map::mapped_type{};
        }

        return it->second;
    }

    inline void assign_if_set_and_nonempty(std::string& target,
                                           const std::unordered_map<std::string, std::string>& haystack,
                                           StringLiteral needle)
    {
        auto it = haystack.find(needle.to_string());
        if (it != haystack.end() && !it->second.empty())
        {
            target = it->second;
        }
    }

    template<class T>
    void assign_if_set_and_nonempty(Optional<T>& target,
                                    const std::unordered_map<std::string, std::string>& haystack,
                                    StringLiteral needle)
    {
        auto it = haystack.find(needle.to_string());
        if (it != haystack.end() && !it->second.empty())
        {
            target.emplace(it->second);
        }
    }

    inline const std::string* value_if_set_and_nonempty(const std::unordered_map<std::string, std::string>& haystack,
                                                        StringLiteral needle)
    {
        auto it = haystack.find(needle.to_string());
        if (it != haystack.end() && !it->second.empty())
        {
            return &it->second;
        }

        return nullptr;
    }

    template<class Map, class Key>
    Optional<const typename Map::mapped_type&> lookup_value(const Map& map, Key&& key)
    {
        const auto it = map.find(static_cast<Key&&>(key));
        if (it == map.end())
        {
            return nullopt;
        }

        return it->second;
    }

    template<class Map, class Key>
    Optional<typename Map::mapped_type> lookup_value_copy(const Map& map, Key&& key)
    {
        const auto it = map.find(static_cast<Key&&>(key));
        if (it == map.end())
        {
            return nullopt;
        }

        return it->second;
    }

    template<class Range, class Pred>
    std::vector<ElementT<const Range&>> filter(const Range& xs, Pred f)
    {
        std::vector<ElementT<const Range&>> ret;

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

    template<class Container, class Pred>
    void erase_if(Container& container, Pred pred)
    {
        for (auto i = container.begin(), last = container.end(); i != last;)
        {
            if (pred(*i))
            {
                i = container.erase(i);
            }
            else
            {
                ++i;
            }
        }
    }

    template<class Pred, class... VectorArgs>
    void erase_if(std::vector<VectorArgs...>& container, Pred pred)
    {
        Util::erase_remove_if(container, pred);
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
        using std::begin;
        using std::end;

        return ::vcpkg::Util::search_and_skip(first, last, begin(rng), end(rng));
    }

    template<class ForwardIt1, class ForwardRange2>
    void search_and_skip(ForwardIt1 first, ForwardIt1 last, const char*) = delete;

    template<class ForwardIt1, class ForwardRange2>
    ForwardIt1 search(ForwardIt1 first, ForwardIt1 last, const ForwardRange2& rng)
    {
        using std::begin;
        using std::end;

        return std::search(first, last, begin(rng), end(rng));
    }

    template<class ForwardIt1, class ForwardRange2>
    void search(ForwardIt1 first, ForwardIt1 last, const char*) = delete;

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
        using std::begin;
        using std::end;

        return find_nth(begin(r), end(r), v, n);
    }

    // 0th is the last occurence
    // so find_nth({1, 2, 1, 3, 1, 4}, 1, 2)
    // returns the 1 before the 2
    template<class R, class V>
    auto find_nth_from_last(R& r, const V& v, size_t n)
    {
        using std::end;
        using std::rbegin;
        using std::rend;

        auto res = find_nth(rbegin(r), rend(r), v, n);
        return res == rend(r) ? end(r) : res.base() - 1;
    }

    template<class Container, class V>
    auto find(Container&& cont, V&& v)
    {
        using std::begin;
        using std::end;
        return std::find(begin(cont), end(cont), v);
    }

    template<class Range, class T>
    bool contains(const Range& r, const T& el)
    {
        using std::end;
        return Util::find(r, el) != end(r);
    }

    template<class Container, class Pred>
    auto find_if(Container&& cont, Pred pred)
    {
        using std::begin;
        using std::end;
        // allow cont.begin() to not have the same type as cont.end()
        auto it = begin(cont);
        auto last = end(cont);
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
        using std::begin;
        using std::end;
        return std::find_if_not(begin(cont), end(cont), pred);
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
        using std::begin;
        using std::end;
        std::sort(begin(cont), end(cont), comp);
    }

    template<class Range, class Pred>
    bool any_of(Range&& rng, Pred pred)
    {
        return std::any_of(rng.begin(), rng.end(), std::move(pred));
    }

    template<class Range, class Comp = std::less<>>
    Range&& sort_unique_erase(Range&& cont, Comp comp = Comp())
    {
        using std::begin;
        using std::end;
        std::sort(begin(cont), end(cont), comp);
        cont.erase(std::unique(begin(cont), end(cont)), end(cont));

        return std::forward<Range>(cont);
    }

    template<class Range1, class Range2>
    bool all_equal(const Range1& r1, const Range2& r2)
    {
        using std::begin;
        using std::end;
        return std::equal(begin(r1), end(r1), begin(r2), end(r2));
    }

    template<class AssocContainer, class K = std::decay_t<decltype(begin(std::declval<AssocContainer>())->first)>>
    std::vector<K> extract_keys(AssocContainer&& input_map)
    {
        return fmap(input_map, [](auto&& p) { return p.first; });
    }

    template<class Range1, class Range2, class Comp>
    int range_lexcomp(const Range1& r1, const Range2& r2, Comp cmp)
    {
        using std::begin;
        using std::end;
        static_assert(std::is_same_v<int, decltype(cmp(*begin(r1), *begin(r2)))>,
                      "Comp must return 'int' (negative for less, 0 for equal, positive for greater)");

        auto a_cur = begin(r1);
        auto a_end = end(r1);

        auto b_cur = begin(r2);
        auto b_end = end(r2);

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
    bool zero_or_one_set(const BoolIsh&... boolish)
    {
        unsigned int total = (0u + ... + static_cast<unsigned int>(static_cast<bool>(boolish)));
        return total <= 1;
    }

    namespace Enum
    {
        template<class E>
        E to_enum(bool b)
        {
            return b ? E::Yes : E::No;
        }

        template<class E>
        bool to_bool(E e)
        {
            return e == E::Yes;
        }
    }
}
