#include <vcpkg/base/api-stable-format.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/unicode.h>
#include <vcpkg/base/util.h>

#include <ctype.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

using namespace vcpkg;

namespace vcpkg::Strings::details
{
    void append_internal(std::string& into, char c) { into += c; }
    void append_internal(std::string& into, const char* v) { into.append(v); }
    void append_internal(std::string& into, const std::string& s) { into.append(s); }
    void append_internal(std::string& into, StringView s) { into.append(s.begin(), s.end()); }
}

Optional<std::string> vcpkg::details::api_stable_format_impl(DiagnosticContext& context,
                                                             StringView sv,
                                                             bool (*cb)(void*, std::string&, StringView),
                                                             void* user)
{
    // Transforms similarly to std::format -- "{xyz}" -> f(xyz), "{{" -> "{", "}}" -> "}"

    static constexpr char s_brackets[] = "{}";

    std::string out;
    auto prev = sv.begin();
    const auto last = sv.end();
    for (const char* p = std::find_first_of(prev, last, s_brackets, s_brackets + 2); p != last;
         p = std::find_first_of(p, last, s_brackets, s_brackets + 2))
    {
        // p[0] == '{' or p[0] == '}'
        out.append(prev, p);
        const char ch = p[0];
        ++p;
        if (ch == '{')
        {
            if (p == last)
            {
                context.report_error(msg::format(msgInvalidFormatString, msg::actual = sv));
                return nullopt;
            }
            else if (*p == '{')
            {
                out.push_back('{');
                prev = ++p;
            }
            else
            {
                // Opened a group
                const auto seq_start = p;
                p = std::find_first_of(p, last, s_brackets, s_brackets + 2);
                if (p == last || p[0] != '}')
                {
                    context.report_error(msg::format(msgInvalidFormatString, msg::actual = sv));
                    return nullopt;
                }
                // p[0] == '}'
                if (!cb(user, out, {seq_start, p}))
                {
                    return nullopt;
                }

                prev = ++p;
            }
        }
        else if (ch == '}')
        {
            if (p == last || p[0] != '}')
            {
                context.report_error(msg::format(msgInvalidFormatString, msg::actual = sv));
                return nullopt;
            }
            out.push_back('}');
            prev = ++p;
        }
    }

    out.append(prev, last);
    return out;
}

namespace
{
    // To disambiguate between two overloads
    constexpr struct
    {
        bool operator()(char c) const noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
    } is_space_char;

    constexpr struct
    {
        char operator()(char c) const noexcept { return (c < 'a' || c > 'z') ? c : c - 'a' + 'A'; }
    } to_upper_char;

    constexpr struct
    {
        char operator()(char c) const noexcept { return (c < 'A' || c > 'Z') ? c : c - 'A' + 'a'; }
    } tolower_char;

    constexpr struct
    {
        bool operator()(char a, char b) const noexcept { return tolower_char(a) == tolower_char(b); }
    } icase_eq;

    constexpr struct
    {
        bool operator()(char a, char b) const noexcept { return tolower_char(a) < tolower_char(b); }
    } icase_less;

}

#if defined(_WIN32)
std::wstring Strings::to_utf16(StringView s)
{
    std::wstring output;
    if (s.size() == 0) return output;
    Checks::check_exit(VCPKG_LINE_INFO, s.size() < size_t(INT_MAX));
    int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    output.resize(static_cast<size_t>(size));
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), output.data(), size);
    return output;
}
#endif

#if defined(_WIN32)
std::string Strings::to_utf8(const wchar_t* w) { return Strings::to_utf8(w, wcslen(w)); }

std::string Strings::to_utf8(const wchar_t* w, size_t size_in_characters)
{
    std::string output;
    to_utf8(output, w, size_in_characters);
    return output;
}

void Strings::to_utf8(std::string& output, const wchar_t* w, size_t size_in_characters)
{
    if (size_in_characters == 0)
    {
        output.clear();
        return;
    }

    vcpkg::Checks::check_exit(VCPKG_LINE_INFO, size_in_characters <= INT_MAX);
    const int s_clamped = static_cast<int>(size_in_characters);
    const int size = WideCharToMultiByte(CP_UTF8, 0, w, s_clamped, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        unsigned long last_error = ::GetLastError();
        Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                      msg::format(msgUtf8ConversionFailed)
                                          .append_raw(std::system_category().message(static_cast<int>(last_error)))
                                          .append_raw('\n')
                                          .append_raw(std::system_category().message(static_cast<int>(last_error))));
    }

    output.resize(size);
    vcpkg::Checks::check_exit(
        VCPKG_LINE_INFO, size == WideCharToMultiByte(CP_UTF8, 0, w, s_clamped, output.data(), size, nullptr, nullptr));
}

std::string Strings::to_utf8(const std::wstring& ws) { return to_utf8(ws.data(), ws.size()); }
#endif

const char* Strings::case_insensitive_ascii_search(StringView s, StringView pattern) noexcept
{
    return std::search(s.begin(), s.end(), pattern.begin(), pattern.end(), icase_eq);
}

bool Strings::case_insensitive_ascii_contains(StringView s, StringView pattern) noexcept
{
    return case_insensitive_ascii_search(s, pattern) != s.end();
}

bool Strings::case_insensitive_ascii_equals(StringView left, StringView right) noexcept
{
    return std::equal(left.begin(), left.end(), right.begin(), right.end(), icase_eq);
}

bool Strings::case_insensitive_ascii_less(StringView left, StringView right) noexcept
{
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(), icase_less);
}

void Strings::inplace_ascii_to_lowercase(char* first, char* last) { std::transform(first, last, first, tolower_char); }

void Strings::inplace_ascii_to_lowercase(std::string& s)
{
    Strings::inplace_ascii_to_lowercase(s.data(), s.data() + s.size());
}

std::string Strings::ascii_to_lowercase(StringView s)
{
    std::string result;
    std::transform(s.begin(), s.end(), std::back_inserter(result), tolower_char);
    return result;
}

std::string Strings::ascii_to_uppercase(StringView s)
{
    std::string result;
    std::transform(s.begin(), s.end(), std::back_inserter(result), to_upper_char);
    return result;
}

bool Strings::case_insensitive_ascii_starts_with(StringView s, StringView pattern)
{
    if (s.size() < pattern.size()) return false;
    return std::equal(s.begin(), s.begin() + pattern.size(), pattern.begin(), pattern.end(), icase_eq);
}

bool Strings::case_insensitive_ascii_ends_with(StringView s, StringView pattern)
{
    if (s.size() < pattern.size()) return false;
    return std::equal(s.end() - pattern.size(), s.end(), pattern.begin(), pattern.end(), icase_eq);
}

bool Strings::ends_with(StringView s, StringView pattern) { return s.ends_with(pattern); }

bool Strings::starts_with(StringView s, StringView pattern) { return s.starts_with(pattern); }

std::string Strings::replace_all(StringView s, StringView search, StringView rep)
{
    std::string ret = s.to_string();
    Strings::inplace_replace_all(ret, search, rep);
    return ret;
}

std::string Strings::replace_all(std::string&& s, StringView search, StringView rep)
{
    inplace_replace_all(s, search, rep);
    return std::move(s);
}

void Strings::inplace_replace_all(std::string& s, StringView search, StringView rep)
{
    if (search.empty())
    {
        return;
    }

    size_t pos = 0;
    while ((pos = s.find(search.data(), pos, search.size())) != std::string::npos)
    {
        s.replace(pos, search.size(), rep.data(), rep.size());
        pos += rep.size();
    }
}

void Strings::inplace_replace_all(std::string& s, char search, char rep) noexcept
{
    std::replace(s.begin(), s.end(), search, rep);
}

void Strings::inplace_trim(std::string& s)
{
    inplace_trim_end(s);
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), is_space_char));
}

void Strings::inplace_trim_end(std::string& s)
{
    s.erase(std::find_if_not(s.rbegin(), s.rend(), is_space_char).base(), s.end());
}

StringView Strings::trim(StringView sv)
{
    auto last = std::find_if_not(sv.rbegin(), sv.rend(), is_space_char).base();
    auto first = std::find_if_not(sv.begin(), sv.end(), is_space_char);
    return StringView(first, last);
}

StringView Strings::trim_end(StringView sv)
{
    auto last = std::find_if_not(sv.rbegin(), sv.rend(), is_space_char).base();
    return StringView(sv.begin(), last);
}

void Strings::inplace_trim_all_and_remove_whitespace_strings(std::vector<std::string>& strings)
{
    for (std::string& s : strings)
    {
        inplace_trim(s);
    }

    Util::erase_remove_if(strings, [](const std::string& s) { return s.empty(); });
}

std::vector<std::string> Strings::split(StringView s, const char delimiter)
{
    std::vector<std::string> output;
    auto first = s.begin();
    const auto last = s.end();
    for (;;)
    {
        first = std::find_if(first, last, [=](const char c) { return c != delimiter; });
        if (first == last)
        {
            return output;
        }

        auto next = std::find(first, last, delimiter);
        output.emplace_back(first, next);
        first = next;
    }
}

std::vector<std::string> Strings::split_keep_empty(StringView s, const char delimiter)
{
    std::vector<std::string> output;
    auto first = s.begin();
    const auto last = s.end();
    do
    {
        auto next = std::find_if(first, last, [=](const char c) { return c == delimiter; });
        output.emplace_back(first, next);
        if (next == last) return output;
        first = next + 1;
    } while (1);
}

std::vector<std::string> Strings::split_paths(StringView s)
{
#if defined(_WIN32)
    return Strings::split(s, ';');
#else // ^^^ defined(_WIN32) // !defined(_WIN32) vvv
    return Strings::split(s, ':');
#endif
}

const char* Strings::find_first_of(StringView input, StringView chars)
{
    return std::find_first_of(input.begin(), input.end(), chars.begin(), chars.end());
}

std::string::size_type Strings::find_last(StringView searched, char c)
{
    auto iter = std::find(searched.rbegin(), searched.rend(), c);
    return iter == searched.rend() ? std::string::npos : (&*iter - searched.begin());
}

std::vector<StringView> Strings::find_all_enclosed(StringView input, StringView left_delim, StringView right_delim)
{
    auto it_left = input.begin();
    auto it_right = input.begin();

    std::vector<StringView> output;

    for (;;)
    {
        it_left = std::search(it_right, input.end(), left_delim.begin(), left_delim.end());
        if (it_left == input.end()) break;

        it_left += left_delim.size();

        it_right = std::search(it_left, input.end(), right_delim.begin(), right_delim.end());
        if (it_right == input.end()) break;

        output.emplace_back(it_left, it_right);

        ++it_right;
    }

    return output;
}

StringView Strings::find_exactly_one_enclosed(StringView input, StringView left_tag, StringView right_tag)
{
    std::vector<StringView> result = find_all_enclosed(input, left_tag, right_tag);
    Checks::msg_check_maybe_upgrade(VCPKG_LINE_INFO,
                                    result.size() == 1,
                                    msgExpectedOneSetOfTags,
                                    msg::count = result.size(),
                                    msg::old_value = left_tag,
                                    msg::new_value = right_tag,
                                    msg::value = input);
    return result.front();
}

Optional<StringView> Strings::find_at_most_one_enclosed(StringView input, StringView left_tag, StringView right_tag)
{
    std::vector<StringView> result = find_all_enclosed(input, left_tag, right_tag);
    Checks::msg_check_maybe_upgrade(VCPKG_LINE_INFO,
                                    result.size() <= 1,
                                    msgExpectedAtMostOneSetOfTags,
                                    msg::count = result.size(),
                                    msg::old_value = left_tag,
                                    msg::new_value = right_tag,
                                    msg::value = input);

    if (result.empty())
    {
        return nullopt;
    }

    return result.front();
}

bool vcpkg::Strings::contains_any_ignoring_c_comments(const std::string& source, View<vcpkg_searcher> to_find)
{
    std::string::size_type offset = 0;
    std::string::size_type no_comment_offset = 0;
    while (offset != std::string::npos)
    {
        no_comment_offset = std::max(offset, no_comment_offset);
        auto start = source.find_first_of("/\"", no_comment_offset);
        if (start == std::string::npos || start + 1 == source.size() || no_comment_offset == std::string::npos)
        {
            return Strings::long_string_contains_any(StringView(source).substr(offset), to_find);
        }

        if (source[start] == '/')
        {
            if (source[start + 1] == '/' || source[start + 1] == '*')
            {
                if (Strings::long_string_contains_any(StringView(source).substr(offset, start - offset), to_find))
                {
                    return true;
                }
                if (source[start + 1] == '/')
                {
                    offset = source.find_first_of('\n', start);
                    while (offset != std::string::npos && source[offset - 1] == '\\')
                        offset = source.find_first_of('\n', offset + 1);
                    if (offset != std::string::npos) ++offset;
                    continue;
                }
                offset = source.find_first_of('/', start + 1);
                while (offset != std::string::npos && source[offset - 1] != '*')
                    offset = source.find_first_of('/', offset + 1);
                if (offset != std::string::npos) ++offset;
                continue;
            }
        }
        else if (source[start] == '\"')
        {
            if (start > 0 && source[start - 1] == 'R') // raw string literals
            {
                auto end = source.find_first_of('(', start);
                if (end == std::string::npos)
                {
                    // invalid c++, but allowed: auto test = 'R"'
                    no_comment_offset = start + 1;
                    continue;
                }
                auto d_char_sequence = ')' + source.substr(start + 1, end - start - 1);
                d_char_sequence.push_back('\"');
                no_comment_offset = source.find(d_char_sequence, end);
                if (no_comment_offset != std::string::npos) no_comment_offset += d_char_sequence.size();
                continue;
            }
            no_comment_offset = source.find_first_of('"', start + 1);
            while (no_comment_offset != std::string::npos && source[no_comment_offset - 1] == '\\')
                no_comment_offset = source.find_first_of('"', no_comment_offset + 1);
            if (no_comment_offset != std::string::npos) ++no_comment_offset;
            continue;
        }
        no_comment_offset = start + 1;
    }
    return false;
}

bool Strings::contains_any_ignoring_hash_comments(StringView source, View<vcpkg_searcher> to_find)
{
    auto first = source.data();
    auto block_start = first;
    const auto last = first + source.size();
    for (; first != last; ++first)
    {
        if (*first == '#')
        {
            if (Strings::long_string_contains_any(StringView{block_start, first}, to_find))
            {
                return true;
            }

            first = std::find(first, last, '\n'); // skip comment
            if (first == last)
            {
                return false;
            }

            block_start = first;
        }
    }

    return Strings::long_string_contains_any(StringView{block_start, last}, to_find);
}

bool Strings::long_string_contains_any(StringView source, View<vcpkg_searcher> to_find)
{
    return std::any_of(to_find.begin(), to_find.end(), [&](const vcpkg_searcher& searcher) {
        return searcher.search(source.begin(), source.end()) != source.end();
    });
}

bool Strings::equals(StringView a, StringView b)
{
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

const char* Strings::search(StringView haystack, StringView needle)
{
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end());
}

size_t Strings::byte_edit_distance(StringView a, StringView b)
{
    static constexpr size_t max_string_size = 100;
    // For large strings, give up early to avoid performance problems
    if (a.size() > max_string_size || b.size() > max_string_size)
    {
        if (a == b)
            return 0;
        else
            return std::max(a.size(), b.size());
    }
    if (a.size() == 0 || b.size() == 0) return std::max(a.size(), b.size());

    auto pa = a.data();
    auto pb = b.data();
    size_t sa = a.size();
    size_t sb = b.size();

    // Levenshtein distance (https://en.wikipedia.org/wiki/Levenshtein_distance)
    // The first row of the edit distance matrix has been omitted because it's trivial (counting from 0)
    // Because each subsequent row only depends on the row above, we never need to store the entire matrix
    char d[max_string_size];

    // Useful invariants:
    //   `sa` is sizeof `pa` using iterator `ia`
    //   `sb` is sizeof `pb` using iterator `ib`
    //   `sa` and `sb` are in (0, `max_string_size`]

    // To avoid dealing with edge effects, `ia` == 0 and `ib` == 0 have been unrolled.
    // Comparisons are used as the cost for the diagonal action (substitute/leave unchanged)
    d[0] = pa[0] != pb[0];
    for (size_t ia = 1; ia < sa; ++ia)
        d[ia] = std::min<char>(d[ia - 1] + 1, static_cast<char>(ia + (pa[ia] != pb[0])));

    for (size_t ib = 1; ib < sb; ++ib)
    {
        // The diagonal information (d[ib-1][ia-1]) is used to compute substitution cost and so must be preserved
        char diag = d[0];
        d[0] = std::min<char>(d[0] + 1, static_cast<char>(ib + (pa[0] != pb[ib])));
        for (size_t ia = 1; ia < sa; ++ia)
        {
            auto subst_or_add = std::min<char>(d[ia - 1] + 1, static_cast<char>(diag + (pa[ia] != pb[ib])));
            diag = d[ia];
            d[ia] = std::min<char>(d[ia] + 1, subst_or_add);
        }
    }
    return d[sa - 1];
}

template<>
Optional<int> Strings::strto<int>(StringView sv)
{
    auto opt = strto<long>(sv);
    if (auto p = opt.get())
    {
        if (INT_MIN <= *p && *p <= INT_MAX)
        {
            return static_cast<int>(*p);
        }
    }
    return nullopt;
}

template<>
Optional<unsigned int> Strings::strto<unsigned int>(StringView sv)
{
    auto opt = strto<unsigned long>(sv);
    if (auto p = opt.get())
    {
        if (*p <= UINT_MAX)
        {
            return static_cast<unsigned int>(*p);
        }
    }
    return nullopt;
}

template<>
Optional<long> Strings::strto<long>(StringView sv)
{
    // disallow initial whitespace
    if (sv.empty() || ParserBase::is_whitespace(sv[0]))
    {
        return nullopt;
    }

    auto with_nul_terminator = sv.to_string();

    errno = 0;
    char* endptr = nullptr;
    long res = strtol(with_nul_terminator.c_str(), &endptr, 10);
    if (endptr != with_nul_terminator.data() + with_nul_terminator.size())
    {
        // contains invalid characters
        return nullopt;
    }
    else if (errno == ERANGE)
    {
        return nullopt;
    }

    return res;
}

template<>
Optional<unsigned long> Strings::strto<unsigned long>(StringView sv)
{
    // disallow initial whitespace
    if (sv.empty() || ParserBase::is_whitespace(sv[0]))
    {
        return nullopt;
    }

    auto with_nul_terminator = sv.to_string();

    errno = 0;
    char* endptr = nullptr;
    long res = strtoul(with_nul_terminator.c_str(), &endptr, 10);
    if (endptr != with_nul_terminator.data() + with_nul_terminator.size())
    {
        // contains invalid characters
        return nullopt;
    }
    else if (errno == ERANGE)
    {
        return nullopt;
    }

    return res;
}

template<>
Optional<long long> Strings::strto<long long>(StringView sv)
{
    // disallow initial whitespace
    if (sv.empty() || ParserBase::is_whitespace(sv[0]))
    {
        return nullopt;
    }

    auto with_nul_terminator = sv.to_string();

    errno = 0;
    char* endptr = nullptr;
    long long res = strtoll(with_nul_terminator.c_str(), &endptr, 10);
    if (endptr != with_nul_terminator.data() + with_nul_terminator.size())
    {
        // contains invalid characters
        return nullopt;
    }
    else if (errno == ERANGE)
    {
        return nullopt;
    }

    return res;
}

template<>
Optional<unsigned long long> Strings::strto<unsigned long long>(StringView sv)
{
    // disallow initial whitespace
    if (sv.empty() || ParserBase::is_whitespace(sv[0]))
    {
        return nullopt;
    }

    auto with_nul_terminator = sv.to_string();

    errno = 0;
    char* endptr = nullptr;
    long long res = strtoull(with_nul_terminator.c_str(), &endptr, 10);
    if (endptr != with_nul_terminator.data() + with_nul_terminator.size())
    {
        // contains invalid characters
        return nullopt;
    }
    else if (errno == ERANGE)
    {
        return nullopt;
    }

    return res;
}

template<>
Optional<double> Strings::strto<double>(StringView sv)
{
    // disallow initial whitespace
    if (sv.empty() || ParserBase::is_whitespace(sv[0]))
    {
        return nullopt;
    }

    auto with_nul_terminator = sv.to_string();

    char* endptr = nullptr;
    double res = strtod(with_nul_terminator.c_str(), &endptr);
    if (endptr != with_nul_terminator.data() + with_nul_terminator.size())
    {
        // contains invalid characters
        return nullopt;
    }
    // else, we may have HUGE_VAL but we expect the caller to deal with that
    return res;
}

namespace vcpkg::Strings
{
    std::string b32_encode(std::uint64_t value) noexcept
    {
        // 32 values, plus the implicit \0
        constexpr static char map[33] = "ABCDEFGHIJKLMNOP"
                                        "QRSTUVWXYZ234567";

        // log2(32)
        constexpr static int shift = 5;
        // 32 - 1
        constexpr static auto mask = 31;

        // ceiling(bitsize(Integral) / log2(32))
        constexpr static auto result_size = (sizeof(value) * CHAR_BIT + shift - 1) / shift;

        std::string result;
        for (std::size_t i = 0; i < result_size; ++i)
        {
            result.push_back(map[value & mask]);
            value >>= shift;
        }

        return result;
    }

    std::string percent_encode(StringView sv) noexcept
    {
        std::string result;
        for (auto c : sv)
        {
            // list from https://datatracker.ietf.org/doc/html/rfc3986#section-2.3
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                c == '_' || c == '~')
            {
                result.push_back(c);
            }
            else
            {
                fmt::format_to(std::back_inserter(result), FMT_COMPILE("%{:02X}"), static_cast<uint8_t>(c));
            }
        }
        return result;
    }

    struct LinesCollector::CB
    {
        LinesCollector* parent;

        void operator()(const StringView sv) const { parent->lines.push_back(sv.to_string()); }
    };

    void LinesCollector::on_data(StringView sv) { stream.on_data(sv, CB{this}); }
    std::vector<std::string> LinesCollector::extract()
    {
        stream.on_end(CB{this});
        return std::move(this->lines);
    }
}
