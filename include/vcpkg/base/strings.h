#pragma once

#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/to_string.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <vector>

namespace vcpkg::Strings::details
{
    // first looks up to_string on `T` using ADL; then, if that isn't found,
    // uses the above definition which returns t.to_string()
    template<class T, class = std::enable_if_t<!std::is_arithmetic<T>::value>>
    auto to_printf_arg(const T& t) -> decltype(to_string(t))
    {
        return to_string(t);
    }

    inline const char* to_printf_arg(const std::string& s) { return s.c_str(); }

    inline const char* to_printf_arg(const char* s) { return s; }

    inline const wchar_t* to_printf_arg(const wchar_t* s) { return s; }

    template<class T, class = std::enable_if_t<std::is_arithmetic<T>::value>>
    T to_printf_arg(T s)
    {
        return s;
    }

    std::string format_internal(const char* fmtstr, ...);

    inline void append_internal(std::string& into, char c) { into += c; }
    template<class T, class = decltype(std::to_string(std::declval<T>()))>
    inline void append_internal(std::string& into, T x)
    {
        into += std::to_string(x);
    }
    inline void append_internal(std::string& into, const char* v) { into.append(v); }
    inline void append_internal(std::string& into, const std::string& s) { into.append(s); }
    inline void append_internal(std::string& into, StringView s) { into.append(s.begin(), s.end()); }
    inline void append_internal(std::string& into, LineInfo ln)
    {
        into.append(ln.file_name);
        into.push_back(':');
        into += std::to_string(ln.line_number);
        into.push_back(':');
    }

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

    template<class Arg>
    std::string& append(std::string& into, const Arg& a)
    {
        details::append_internal(into, a);
        return into;
    }
    template<class Arg, class... Args>
    std::string& append(std::string& into, const Arg& a, const Args&... args)
    {
        append(into, a);
        return append(into, args...);
    }

    template<class... Args>
    [[nodiscard]] std::string concat(const Args&... args)
    {
        std::string ret;
        append(ret, args...);
        return ret;
    }

    template<class... Args>
    [[nodiscard]] std::string concat(std::string&& first, const Args&... args)
    {
        append(first, args...);
        return std::move(first);
    }

    template<class... Args, class = void>
    std::string concat_or_view(const Args&... args)
    {
        return Strings::concat(args...);
    }

    template<class T, class = std::enable_if_t<std::is_convertible<T, StringView>::value>>
    StringView concat_or_view(const T& v)
    {
        return v;
    }

    template<class... Args>
    std::string format(const char* fmtstr, const Args&... args)
    {
        using vcpkg::Strings::details::to_printf_arg;
        return details::format_internal(fmtstr, to_printf_arg(to_printf_arg(args))...);
    }

#if defined(_WIN32)
    std::wstring to_utf16(StringView s);

    std::string to_utf8(const wchar_t* w);
    std::string to_utf8(const wchar_t* w, size_t size_in_characters);
    void to_utf8(std::string& output, const wchar_t* w, size_t size_in_characters);
    std::string to_utf8(const std::wstring& ws);
#endif

    std::string escape_string(std::string&& s, char char_to_escape, char escape_char);

    const char* case_insensitive_ascii_search(StringView s, StringView pattern);
    bool case_insensitive_ascii_contains(StringView s, StringView pattern);
    bool case_insensitive_ascii_equals(StringView left, StringView right);

    void ascii_to_lowercase(char* first, char* last);
    std::string ascii_to_lowercase(std::string&& s);

    std::string ascii_to_uppercase(std::string&& s);

    bool case_insensitive_ascii_starts_with(StringView s, StringView pattern);
    bool case_insensitive_ascii_ends_with(StringView s, StringView pattern);
    bool ends_with(StringView s, StringView pattern);
    bool starts_with(StringView s, StringView pattern);

    template<class InputIterator, class Transformer>
    std::string join(StringLiteral delimiter, InputIterator begin, InputIterator end, Transformer transformer)
    {
        if (begin == end)
        {
            return std::string();
        }

        std::string output;
        append(output, transformer(*begin));
        for (auto it = std::next(begin); it != end; ++it)
        {
            output.append(delimiter.data(), delimiter.size());
            append(output, transformer(*it));
        }

        return output;
    }

    template<class Container, class Transformer>
    std::string join(StringLiteral delimiter, const Container& v, Transformer transformer)
    {
        const auto begin = std::begin(v);
        const auto end = std::end(v);

        return join(delimiter, begin, end, transformer);
    }

    template<class InputIterator>
    std::string join(StringLiteral delimiter, InputIterator begin, InputIterator end)
    {
        using Element = decltype(*begin);
        return join(delimiter, begin, end, [](const Element& x) -> const Element& { return x; });
    }

    template<class Container>
    std::string join(StringLiteral delimiter, const Container& v)
    {
        using Element = decltype(*std::begin(v));
        return join(delimiter, v, [](const Element& x) -> const Element& { return x; });
    }

    [[nodiscard]] std::string replace_all(const char* s, StringView search, StringView rep);
    [[nodiscard]] std::string replace_all(StringView s, StringView search, StringView rep);
    [[nodiscard]] std::string replace_all(std::string&& s, StringView search, StringView rep);

    void inplace_replace_all(std::string& s, StringView search, StringView rep);

    void inplace_replace_all(std::string& s, char search, char rep) noexcept;

    std::string trim(std::string&& s);

    StringView trim(StringView sv);

    void trim_all_and_remove_whitespace_strings(std::vector<std::string>* strings);

    std::vector<std::string> split(StringView s, const char delimiter);

    std::vector<std::string> split_paths(StringView s);

    const char* find_first_of(StringView searched, StringView candidates);

    std::vector<StringView> find_all_enclosed(StringView input, StringView left_delim, StringView right_delim);

    StringView find_exactly_one_enclosed(StringView input, StringView left_tag, StringView right_tag);

    Optional<StringView> find_at_most_one_enclosed(StringView input, StringView left_tag, StringView right_tag);

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
    Optional<long> strto<long>(StringView);
    template<>
    Optional<long long> strto<long long>(StringView);
    template<>
    Optional<double> strto<double>(StringView);

    const char* search(StringView haystack, StringView needle);

    bool contains(StringView haystack, StringView needle);
    bool contains(StringView haystack, char needle);

    // base 32 encoding, following IETC RFC 4648
    std::string b32_encode(std::uint64_t x) noexcept;

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
