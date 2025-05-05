#pragma once

#include <vcpkg/base/checks.h>

#include <stddef.h>

namespace vcpkg::Unicode
{
    constexpr static char32_t end_of_file = 0xFFFF'FFFF;

    enum class utf8_errc
    {
        NoError = 0,
        InvalidCodeUnit = 1,
        InvalidCodePoint = 2,
        PairedSurrogates = 3,
        UnexpectedContinue = 4,
        UnexpectedStart = 5,
        UnexpectedEof = 6,
    };

    int utf8_encode_code_point(char (&array)[4], char32_t code_point) noexcept;

    // If possible, decodes one codepoint from the beginning of [first, last). If successful advances first after the
    // last decoded encoding unit, stores the codepoint in out, and returns utf8_errc::NoError.
    // Otherwise, advances first to last, stores end_of_file in out, and returns one of the utf8_errc values.
    utf8_errc utf8_decode_code_point(const char*& first, const char* last, char32_t& out) noexcept;

    // uses the C++20 definition
    /*
        [format.string.std]
        * U+1100 - U+115F
        * U+2329 - U+232A
        * U+2E80 - U+303E
        * U+3040 - U+A4CF
        * U+AC00 - U+D7A3
        * U+F900 - U+FAFF
        * U+FE10 - U+FE19
        * U+FE30 - U+FE6F
        * U+FF00 - U+FF60
        * U+FFE0 - U+FFE6
        * U+1F300 - U+1F64F
        * U+1F900 - U+1F9FF
        * U+20000 - U+2FFFD
        * U+30000 - U+3FFFD
    */
    constexpr bool is_double_width_code_point(char32_t ch) noexcept
    {
        return (ch >= 0x1100 && ch <= 0x115F) || (ch >= 0x2329 && ch <= 0x232A) || (ch >= 0x2E80 && ch <= 0x303E) ||
               (ch >= 0x3040 && ch <= 0xA4CF) || (ch >= 0xAC00 && ch <= 0xD7A3) || (ch >= 0xF900 && ch <= 0xFAFF) ||
               (ch >= 0xFE10 && ch <= 0xFE19) || (ch >= 0xFE30 && ch <= 0xFE6F) || (ch >= 0xFF00 && ch <= 0xFF60) ||
               (ch >= 0xFFE0 && ch <= 0xFFE6) || (ch >= 0x1F300 && ch <= 0x1F64F) || (ch >= 0x1F900 && ch <= 0x1F9FF) ||
               (ch >= 0x20000 && ch <= 0x2FFFD) || (ch >= 0x30000 && ch <= 0x3FFFD);
    }

    inline std::string& utf8_append_code_point(std::string& str, char32_t code_point)
    {
        if (static_cast<uint32_t>(code_point) < 0x80)
        {
            str.push_back(static_cast<char>(code_point));
        }
        else
        {
            char buf[4] = {};
            int count = ::vcpkg::Unicode::utf8_encode_code_point(buf, code_point);
            str.append(buf, buf + count);
        }
        return str;
    }

    bool utf8_is_valid_string(const char* first, const char* last) noexcept;

    constexpr bool utf16_is_leading_surrogate_code_point(char32_t code_point) noexcept
    {
        return code_point >= 0xD800 && code_point < 0xDC00;
    }
    constexpr bool utf16_is_trailing_surrogate_code_point(char32_t code_point) noexcept
    {
        return code_point >= 0xDC00 && code_point < 0xE000;
    }
    constexpr bool utf16_is_surrogate_code_point(char32_t code_point) noexcept
    {
        return code_point >= 0xD800 && code_point < 0xE000;
    }

    char32_t utf16_surrogates_to_code_point(char32_t leading, char32_t trailing) noexcept;

    /*
        There are two ways to parse utf-8: we could allow unpaired surrogates (as in [wtf-8]) -- this is important
        for representing things like file paths on Windows. We could also require strict utf-8, as in the JSON
        specification. We need both, since when parsing JSON, we need to require strict utf-8; however, when
        outputting JSON, we need to be able to stringify unpaired surrogates (as '\uDxyz'). This dichotomy is an
        issue _because_ we need to be able to decode two different kinds of utf-8: utf-8 as read off of a disk
        (strict), and utf-8 as contained in a C++ string (non-strict).

        Since one is a strict superset of the other, we allow the non-strict utf-8 in this decoder; if a consumer
        wishes to make certain that the utf-8 is strictly conforming, it will have to do the check on it's own with
        `utf16_is_surrogate_code_point`.

        [wtf-8]: https://simonsapin.github.io/wtf-8/
    */
    struct Utf8Decoder
    {
        constexpr Utf8Decoder() noexcept
            : current_(end_of_file), pointer_to_current_(nullptr), next_(nullptr), last_(nullptr)
        {
        }
        explicit constexpr Utf8Decoder(StringView sv) noexcept : Utf8Decoder(sv.begin(), sv.end()) { }
        constexpr Utf8Decoder(const char* first, const char* last) noexcept
            : current_(0), pointer_to_current_(first), next_(first), last_(last)
        {
            if (next_ != last_)
            {
                ++*this;
            }
            else
            {
                current_ = end_of_file;
            }
        }
        constexpr Utf8Decoder(StringView sv, utf8_errc& first_decode_error) noexcept
            : Utf8Decoder(sv.begin(), sv.end(), first_decode_error)
        {
        }
        constexpr Utf8Decoder(const char* first, const char* last, utf8_errc& first_decode_error) noexcept
            : current_(0), pointer_to_current_(first), next_(first), last_(last)
        {
            if (next_ != last_)
            {
                first_decode_error = next();
            }
            else
            {
                current_ = end_of_file;
                first_decode_error = utf8_errc::NoError;
            }
        }

        struct sentinel
        {
        };

        constexpr inline bool is_eof() const noexcept { return current_ == end_of_file; }

        [[nodiscard]] utf8_errc next() noexcept;

        Utf8Decoder& operator=(sentinel) noexcept;

        char const* pointer_to_current() const noexcept { return pointer_to_current_; }

        char32_t operator*() const noexcept
        {
            if (is_eof()) Checks::unreachable(VCPKG_LINE_INFO);
            return current_;
        }

        Utf8Decoder& operator++() noexcept;
        Utf8Decoder operator++(int) noexcept
        {
            auto res = *this;
            ++*this;
            return res;
        }

        constexpr Utf8Decoder begin() const { return *this; }

        constexpr sentinel end() const { return sentinel(); }

        friend constexpr bool operator==(const Utf8Decoder& lhs, const Utf8Decoder& rhs) noexcept
        {
            if (lhs.last_ != rhs.last_)
            {
                // comparing decoders of different provenance is always an error
                Checks::unreachable(VCPKG_LINE_INFO);
            }

            return lhs.pointer_to_current_ == rhs.pointer_to_current_;
        }
        friend constexpr bool operator!=(const Utf8Decoder& lhs, const Utf8Decoder& rhs) noexcept
        {
            return !(lhs == rhs);
        }
        friend constexpr bool operator==(const Utf8Decoder& d, Utf8Decoder::sentinel) noexcept { return d.is_eof(); }
        friend constexpr bool operator==(Utf8Decoder::sentinel s, const Utf8Decoder& d) noexcept { return d == s; }
        friend constexpr bool operator!=(const Utf8Decoder& d, Utf8Decoder::sentinel) noexcept { return !d.is_eof(); }
        friend constexpr bool operator!=(Utf8Decoder::sentinel s, const Utf8Decoder& d) noexcept { return d != s; }

        using difference_type = std::ptrdiff_t;
        using value_type = char32_t;
        using pointer = void;
        using reference = char32_t;
        using iterator_category = std::forward_iterator_tag;

    private:
        char32_t current_;
        const char* pointer_to_current_;
        const char* next_;
        const char* last_;
    };
} // namespace vcpkg::Unicode

namespace vcpkg
{
    struct SourceLoc
    {
        Unicode::Utf8Decoder it;
        Unicode::Utf8Decoder start_of_line;
        int row;
        int column;
    };
}
