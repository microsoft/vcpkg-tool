#pragma once

#include <vcpkg/base/fwd/span.h>

#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/to-string.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <vector>

namespace vcpkg::Strings::details
{
    void append_internal(std::string& into, char c);
    void append_internal(std::string& into, const char* v);
    void append_internal(std::string& into, const std::string& s);
    void append_internal(std::string& into, StringView s);
    template<class T, class = decltype(std::declval<const T&>().to_string(std::declval<std::string&>()))>
    void append_internal(std::string& into, const T& t)
    {
        t.to_string(into);
    }
    template<class T, class = void, class = decltype(to_string(std::declval<std::string&>(), std::declval<const T&>()))>
    void append_internal(std::string& into, const T& t)
    {
        to_string(into, t);
    }

    static constexpr struct IdentityTransformer
    {
        template<class T>
        T&& operator()(T&& t) const noexcept
        {
            return static_cast<T&&>(t);
        }
    } identity_transformer;
}

namespace vcpkg::Strings
{
#ifdef __cpp_lib_boyer_moore_searcher
    struct vcpkg_searcher
    {
        vcpkg_searcher(std::string::const_iterator first, std::string::const_iterator last) : impl(first, last) { }

        template<class SearchIt>
        SearchIt search(SearchIt first, SearchIt last) const noexcept
        {
            return std::search(first, last, impl);
        }

    private:
        std::boyer_moore_horspool_searcher<std::string::const_iterator> impl;
    };
#else
    struct vcpkg_searcher
    {
        vcpkg_searcher(std::string::const_iterator first, std::string::const_iterator last)
            : first_pattern(first), last_pattern(last)
        {
        }

        template<class SearchIt>
        SearchIt search(SearchIt first, SearchIt last) const noexcept
        {
            return std::search(first, last, first_pattern, last_pattern);
        }

    private:
        std::string::const_iterator first_pattern;
        std::string::const_iterator last_pattern;
    };
#endif

    template<class... Args>
    std::string& append(std::string& into, const Args&... args)
    {
        (void)((details::append_internal(into, args), 0) || ... || 0);
        return into;
    }

    template<class... Args>
    [[nodiscard]] std::string concat(const Args&... args)
    {
        std::string into;
        (void)((details::append_internal(into, args), 0) || ... || 0);
        return into;
    }

    template<class... Args>
    [[nodiscard]] std::string concat(std::string&& into, const Args&... args)
    {
        (void)((details::append_internal(into, args), 0) || ... || 0);
        return std::move(into);
    }

#if defined(_WIN32)
    std::wstring to_utf16(StringView s);

    std::string to_utf8(const wchar_t* w);
    std::string to_utf8(const wchar_t* w, size_t size_in_characters);
    void to_utf8(std::string& output, const wchar_t* w, size_t size_in_characters);
    std::string to_utf8(const std::wstring& ws);
#endif

    const char* case_insensitive_ascii_search(StringView s, StringView pattern) noexcept;
    bool case_insensitive_ascii_contains(StringView s, StringView pattern) noexcept;
    bool case_insensitive_ascii_equals(StringView left, StringView right) noexcept;
    bool case_insensitive_ascii_less(StringView left, StringView right) noexcept;

    void inplace_ascii_to_lowercase(char* first, char* last);
    void inplace_ascii_to_lowercase(std::string& s);
    [[nodiscard]] std::string ascii_to_lowercase(StringView s);
    [[nodiscard]] std::string ascii_to_uppercase(StringView s);

    bool case_insensitive_ascii_starts_with(StringView s, StringView pattern);
    bool case_insensitive_ascii_ends_with(StringView s, StringView pattern);
    bool ends_with(StringView s, StringView pattern);
    bool starts_with(StringView s, StringView pattern);

    template<class InputIterator, class Transformer>
    [[nodiscard]] std::string join(StringLiteral delimiter,
                                   InputIterator first,
                                   InputIterator last,
                                   Transformer transformer)
    {
        std::string output;
        if (first == last)
        {
            return output;
        }

        for (;;)
        {
            Strings::append(output, transformer(*first));
            if (++first == last)
            {
                return output;
            }

            output.append(delimiter.data(), delimiter.size());
        }
    }

    template<class Container, class Transformer>
    [[nodiscard]] std::string join(StringLiteral delimiter, const Container& v, Transformer transformer)
    {
        return join(delimiter, std::begin(v), std::end(v), transformer);
    }

    template<class InputIterator>
    [[nodiscard]] std::string join(StringLiteral delimiter, InputIterator first, InputIterator last)
    {
        return join(delimiter, first, last, details::identity_transformer);
    }

    template<class Container>
    [[nodiscard]] std::string join(StringLiteral delimiter, const Container& v)
    {
        return join(delimiter, std::begin(v), std::end(v), details::identity_transformer);
    }

    [[nodiscard]] std::string replace_all(StringView s, StringView search, StringView rep);
    [[nodiscard]] std::string replace_all(std::string&& s, StringView search, StringView rep);

    void inplace_replace_all(std::string& s, StringView search, StringView rep);

    void inplace_replace_all(std::string& s, char search, char rep) noexcept;

    void inplace_trim(std::string& s);

    void inplace_trim_end(std::string& s);

    [[nodiscard]] StringView trim(StringView sv);

    [[nodiscard]] StringView trim_end(StringView sv);

    void inplace_trim_all_and_remove_whitespace_strings(std::vector<std::string>& strings);

    [[nodiscard]] std::vector<std::string> split(StringView s, const char delimiter);

    [[nodiscard]] std::vector<std::string> split_keep_empty(StringView s, const char delimiter);

    [[nodiscard]] std::vector<std::string> split_paths(StringView s);

    const char* find_first_of(StringView searched, StringView candidates);

    [[nodiscard]] std::string::size_type find_last(StringView searched, char c);

    [[nodiscard]] std::vector<StringView> find_all_enclosed(StringView input,
                                                            StringView left_delim,
                                                            StringView right_delim);

    [[nodiscard]] StringView find_exactly_one_enclosed(StringView input, StringView left_tag, StringView right_tag);

    [[nodiscard]] Optional<StringView> find_at_most_one_enclosed(StringView input,
                                                                 StringView left_tag,
                                                                 StringView right_tag);

    bool contains_any_ignoring_c_comments(const std::string& source, View<vcpkg_searcher> to_find);

    bool contains_any_ignoring_hash_comments(StringView source, View<vcpkg_searcher> to_find);

    bool long_string_contains_any(StringView source, View<vcpkg_searcher> to_find);

    [[nodiscard]] bool equals(StringView a, StringView b);

    template<class T>
    [[nodiscard]] std::string serialize(const T& t)
    {
        std::string ret;
        serialize(t, ret);
        return ret;
    }

    // Equivalent to one of the `::strto[T]` functions. Returns `nullopt` if there is an error.
    template<class T>
    Optional<T> strto(StringView sv);

    template<>
    Optional<int> strto<int>(StringView);
    template<>
    Optional<unsigned int> strto<unsigned int>(StringView);
    template<>
    Optional<long> strto<long>(StringView);
    template<>
    Optional<unsigned long> strto<unsigned long>(StringView);
    template<>
    Optional<long long> strto<long long>(StringView);
    template<>
    Optional<unsigned long long> strto<unsigned long long>(StringView);
    template<>
    Optional<double> strto<double>(StringView);

    const char* search(StringView haystack, StringView needle);

    // base 32 encoding, following IETF RFC 4648
    [[nodiscard]] std::string b32_encode(std::uint64_t x) noexcept;

    // percent encoding, following IETF RFC 3986
    [[nodiscard]] std::string percent_encode(StringView sv) noexcept;

    // Implements https://en.wikipedia.org/wiki/Levenshtein_distance with a "give-up" clause for large strings
    // Guarantees 0 for equal strings and nonzero for inequal strings.
    size_t byte_edit_distance(StringView a, StringView b);

    struct LinesStream
    {
        template<class Fn>
        void on_data(const StringView sv, Fn cb)
        {
            const auto last = sv.end();
            auto start = sv.begin();
            for (;;)
            {
                const auto newline = std::find_if(start, last, is_newline);
                if (newline == last)
                {
                    previous_partial_line.append(start, newline);
                    return;
                }
                else if (!previous_partial_line.empty())
                {
                    // include the prefix of this line from the last callback
                    previous_partial_line.append(start, newline);
                    cb(StringView{previous_partial_line});
                    previous_partial_line.clear();
                }
                else if (!last_was_cr || *newline != '\n' || newline != start)
                {
                    // implement \r\n, \r, \n newlines by logically generating all newlines,
                    // and skipping emission of an empty \n newline iff immediately following \r
                    cb(StringView{start, newline});
                }

                last_was_cr = *newline == '\r';
                start = newline + 1;
            }
        }

        template<class Fn>
        void on_end(Fn cb)
        {
            if (!previous_partial_line.empty())
            {
                cb(StringView{previous_partial_line});
                previous_partial_line.clear();
            }
            last_was_cr = false;
        }

    private:
        struct IsNewline
        {
            constexpr bool operator()(const char c) const noexcept { return c == '\n' || c == '\r'; }
        };
        static constexpr IsNewline is_newline{};

        bool last_was_cr = false;
        std::string previous_partial_line;
    };

    struct LinesCollector
    {
        void on_data(StringView sv);
        std::vector<std::string> extract();

    private:
        struct CB;

        LinesStream stream;
        std::vector<std::string> lines;
    };
}
