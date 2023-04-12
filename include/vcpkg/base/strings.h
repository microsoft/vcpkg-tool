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
}

namespace vcpkg::Strings
{
    constexpr struct
    {
        char operator()(char c) const noexcept { return (c < 'A' || c > 'Z') ? c : c - 'A' + 'a'; }
    } tolower_char;

    constexpr struct
    {
        bool operator()(char a, char b) const noexcept { return tolower_char(a) == tolower_char(b); }
    } icase_eq;

    template<class... Args>
    std::string& append(std::string& into, const Args&... args)
    {
        (void)(details::append_internal(into, args), ...);
        return into;
    }

    template<class... Args>
    [[nodiscard]] std::string concat(const Args&... args)
    {
        std::string ret;
        (void)(details::append_internal(ret, args), ...);
        return ret;
    }

    template<class... Args>
    [[nodiscard]] std::string concat(std::string&& first, const Args&... args)
    {
        (void)(details::append_internal(first, args), ...);
        return std::move(first);
    }

    template<class... Args, class = void>
    std::string concat_or_view(const Args&... args)
    {
        return Strings::concat(args...);
    }

    template<class T, class = std::enable_if_t<std::is_convertible_v<T, StringView>>>
    StringView concat_or_view(const T& v)
    {
        return v;
    }

#if defined(_WIN32)
    std::wstring to_utf16(StringView s);

    std::string to_utf8(const wchar_t* w);
    std::string to_utf8(const wchar_t* w, size_t size_in_characters);
    void to_utf8(std::string& output, const wchar_t* w, size_t size_in_characters);
    std::string to_utf8(const std::wstring& ws);
#endif

    const char* case_insensitive_ascii_search(StringView s, StringView pattern);
    bool case_insensitive_ascii_contains(StringView s, StringView pattern);
    bool case_insensitive_ascii_equals(StringView left, StringView right);

    void ascii_to_lowercase(char* first, char* last);
    std::string ascii_to_lowercase(const std::string& s);
    std::string ascii_to_lowercase(std::string&& s);

    std::string ascii_to_uppercase(std::string&& s);

    bool case_insensitive_ascii_starts_with(StringView s, StringView pattern);
    bool case_insensitive_ascii_ends_with(StringView s, StringView pattern);
    bool ends_with(StringView s, StringView pattern);
    bool starts_with(StringView s, StringView pattern);

    template<class InputIterator, class Transformer>
    std::string join(StringLiteral delimiter, InputIterator first, InputIterator last, Transformer transformer)
    {
        if (first == last)
        {
            return std::string();
        }

        std::string output;
        append(output, transformer(*first));
        for (++first; first != last; ++first)
        {
            output.append(delimiter.data(), delimiter.size());
            append(output, transformer(*first));
        }

        return output;
    }

    template<class Container, class Transformer>
    std::string join(StringLiteral delimiter, const Container& v, Transformer transformer)
    {
        return join(delimiter, std::begin(v), std::end(v), transformer);
    }

    template<class InputIterator>
    std::string join(StringLiteral delimiter, InputIterator first, InputIterator last)
    {
        if (first == last)
        {
            return std::string();
        }

        std::string output;
        append(output, *first);
        for (++first; first != last; ++first)
        {
            output.append(delimiter.data(), delimiter.size());
            append(output, *first);
        }

        return output;
    }

    template<class Container>
    std::string join(StringLiteral delimiter, const Container& v)
    {
        return join(delimiter, std::begin(v), std::end(v));
    }

    [[nodiscard]] std::string replace_all(const char* s, StringView search, StringView rep);
    [[nodiscard]] std::string replace_all(StringView s, StringView search, StringView rep);
    [[nodiscard]] std::string replace_all(std::string&& s, StringView search, StringView rep);

    void inplace_replace_all(std::string& s, StringView search, StringView rep);

    void inplace_replace_all(std::string& s, char search, char rep) noexcept;

    void inplace_trim(std::string& s);

    StringView trim(StringView sv);

    void inplace_trim_all_and_remove_whitespace_strings(std::vector<std::string>& strings);

    std::vector<std::string> split(StringView s, const char delimiter);

    std::vector<std::string> split_paths(StringView s);

    const char* find_first_of(StringView searched, StringView candidates);

    std::vector<StringView> find_all_enclosed(StringView input, StringView left_delim, StringView right_delim);

    StringView find_exactly_one_enclosed(StringView input, StringView left_tag, StringView right_tag);

    Optional<StringView> find_at_most_one_enclosed(StringView input, StringView left_tag, StringView right_tag);

    bool contains_any_ignoring_c_comments(const std::string& source, View<StringView> to_find);

    bool contains_any_ignoring_hash_comments(StringView source, View<StringView> to_find);

    bool contains_any(StringView source, View<StringView> to_find);

    bool equals(StringView a, StringView b);

    template<class T>
    std::string serialize(const T& t)
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

    bool contains(StringView haystack, StringView needle);
    bool contains(StringView haystack, char needle);

    // base 32 encoding, following IETF RFC 4648
    std::string b32_encode(std::uint64_t x) noexcept;

    // percent encoding, following IETF RFC 3986
    std::string percent_encode(StringView sv) noexcept;

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
            cb(StringView{previous_partial_line});
            previous_partial_line.clear();
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
