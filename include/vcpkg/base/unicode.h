#pragma once

#include <vcpkg/base/checks.h>

#include <stddef.h>

namespace vcpkg::Unicode
{
    enum class Utf8CodeUnitKind
    {
        Invalid = -1,
        Continue = 0,
        StartOne = 1,
        StartTwo = 2,
        StartThree = 3,
        StartFour = 4,
    };

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

    DECLARE_MESSAGE(Utf8DecoderDereferencedAtEof, (), "", "dereferenced Utf8Decoder at the end of a string.");

    const std::error_category& utf8_category() noexcept;

    Utf8CodeUnitKind utf8_code_unit_kind(unsigned char code_unit) noexcept;
    int utf8_code_unit_count(Utf8CodeUnitKind kind) noexcept;
    int utf8_code_unit_count(char code_unit) noexcept;

    int utf8_encode_code_point(char (&array)[4], char32_t code_point) noexcept;

    // returns {after-current-code-point, error},
    // and if error = NoError, then out = parsed code point.
    // else, out = end_of_file.
    std::pair<const char*, utf8_errc> utf8_decode_code_point(const char* first,
                                                             const char* last,
                                                             char32_t& out) noexcept;

    // uses the C++20 definition
    bool is_double_width_code_point(char32_t ch) noexcept;

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

    constexpr bool utf16_is_leading_surrogate_code_point(char32_t code_point)
    {
        return code_point >= 0xD800 && code_point < 0xDC00;
    }
    constexpr bool utf16_is_trailing_surrogate_code_point(char32_t code_point)
    {
        return code_point >= 0xDC00 && code_point < 0xE000;
    }
    constexpr bool utf16_is_surrogate_code_point(char32_t code_point)
    {
        return code_point >= 0xD800 && code_point < 0xE000;
    }

    char32_t utf16_surrogates_to_code_point(char32_t leading, char32_t trailing);

    inline std::error_code make_error_code(utf8_errc err) noexcept
    {
        return std::error_code(static_cast<int>(err), utf8_category());
    }

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
        Utf8Decoder() noexcept;
        explicit Utf8Decoder(StringView sv) : Utf8Decoder(sv.begin(), sv.end()) { }
        Utf8Decoder(const char* first, const char* last) noexcept;

        struct sentinel
        {
        };

        constexpr inline bool is_eof() const noexcept { return current_ == end_of_file; }

        [[nodiscard]] utf8_errc next();

        Utf8Decoder& operator=(sentinel) noexcept;

        char const* pointer_to_current() const noexcept;

        char32_t operator*() const noexcept
        {
            if (is_eof())
            {
                msg::print(Color::error, msg::msgInternalErrorMessage);
                msg::println(Color::error, msgUtf8DecoderDereferencedAtEof);
                Checks::msg_exit_with_message(VCPKG_LINE_INFO, msg::msgInternalErrorMessageContact);
            }
            return current_;
        }

        Utf8Decoder& operator++() noexcept;
        Utf8Decoder operator++(int) noexcept
        {
            auto res = *this;
            ++*this;
            return res;
        }

        Utf8Decoder begin() const { return *this; }

        sentinel end() const { return sentinel(); }

        friend bool operator==(const Utf8Decoder& lhs, const Utf8Decoder& rhs) noexcept;

        using difference_type = std::ptrdiff_t;
        using value_type = char32_t;
        using pointer = void;
        using reference = char32_t;
        using iterator_category = std::forward_iterator_tag;

    private:
        char32_t current_;
        const char* next_;
        const char* last_;
    };

    inline bool operator!=(const Utf8Decoder& lhs, const Utf8Decoder& rhs) noexcept { return !(lhs == rhs); }

    inline bool operator==(const Utf8Decoder& d, Utf8Decoder::sentinel) { return d.is_eof(); }
    inline bool operator==(Utf8Decoder::sentinel s, const Utf8Decoder& d) { return d == s; }
    inline bool operator!=(const Utf8Decoder& d, Utf8Decoder::sentinel) { return !d.is_eof(); }
    inline bool operator!=(Utf8Decoder::sentinel s, const Utf8Decoder& d) { return d != s; }

}

namespace std
{
    template<>
    struct is_error_code_enum<vcpkg::Unicode::utf8_errc> : std::true_type
    {
    };

}
