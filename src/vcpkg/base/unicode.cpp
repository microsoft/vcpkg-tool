#include <vcpkg/base/checks.h>
#include <vcpkg/base/unicode.h>

namespace vcpkg::Unicode
{
    int utf8_encode_code_point(char (&array)[4], char32_t code_point) noexcept
    {
        if (code_point < 0x80)
        {
            array[0] = static_cast<char>(code_point);
            return 1;
        }

        if (code_point < 0x800)
        {
            array[0] = static_cast<unsigned char>(0b1100'0000u | (code_point >> 6));
            array[1] = static_cast<unsigned char>(0b1000'0000u | (code_point & 0b0011'1111u));
            return 2;
        }

        if (code_point < 0x10000)
        {
            // clang-format off
            array[0] = static_cast<unsigned char>(0b1110'0000u | (code_point >> 12));
            array[1] = static_cast<unsigned char>(0b1000'0000u | ((code_point >> 6) & 0b0011'1111u));
            array[2] = static_cast<unsigned char>(0b1000'0000u | (code_point        & 0b0011'1111u));
            // clang-format on
            return 3;
        }

        if (code_point < 0x110000)
        {
            // clang-format off
            array[0] = static_cast<unsigned char>(0b1111'0000u |  (code_point >> 18));
            array[1] = static_cast<unsigned char>(0b1000'0000u | ((code_point >> 12) & 0b0011'1111u));
            array[2] = static_cast<unsigned char>(0b1000'0000u | ((code_point >> 6)  & 0b0011'1111u));
            array[3] = static_cast<unsigned char>(0b1000'0000u |  (code_point        & 0b0011'1111u));
            // clang-format on
            return 4;
        }

        vcpkg::Checks::msg_exit_with_message(
            VCPKG_LINE_INFO,
            msg::format(msgInvalidCodePoint).append_raw(fmt::format("({:x})", static_cast<uint32_t>(code_point))));
    }

    int utf8_code_unit_count(char raw_code_unit) noexcept
    {
        const auto code_unit = static_cast<unsigned char>(raw_code_unit);
        if (code_unit < 0b1000'0000)
        {
            return 1;
        }

        if (code_unit < 0b1100'0000)
        {
            return 0;
        }

        if (code_unit < 0b1110'0000)
        {
            return 2;
        }

        if (code_unit < 0b1111'0000)
        {
            return 3;
        }

        if (code_unit < 0b1111'1000)
        {
            return 4;
        }

        return -1;
    }

    static bool bad_trailing(unsigned char code_unit, utf8_errc& err) noexcept
    {
        if ((code_unit & 0b1100'0000u) != 0b1000'0000u)
        {
            if (code_unit >= 0b1111'1000u)
            {
                err = utf8_errc::InvalidCodeUnit;
            }

            err = utf8_errc::UnexpectedStart;

            return true;
        }

        return false;
    }

    utf8_errc utf8_decode_code_point(const char*& first, const char* last, char32_t& out) noexcept
    {
        if (first == last)
        {
            out = end_of_file;
            return utf8_errc::NoError;
        }

        auto code_unit = static_cast<unsigned char>(*first);
        if (code_unit < 0b1000'0000u)
        {
            out = code_unit;
            ++first;
            return utf8_errc::NoError;
        }

        if (code_unit < 0b1100'0000u)
        {
            out = end_of_file;
            first = last;
            return utf8_errc::UnexpectedContinue;
        }

        if (code_unit < 0b1110'0000u)
        {
            if (2 > last - first)
            {
                out = end_of_file;
                first = last;
                return utf8_errc::UnexpectedEof;
            }

            utf8_errc out_error;
            if (bad_trailing(static_cast<unsigned char>(first[1]), out_error))
            {
                out = end_of_file;
                first = last;
                return out_error;
            }

            out = ((code_unit & 0b0001'1111) << 6) | (static_cast<unsigned char>(first[1]) & 0b0011'1111u);
            first += 2;
            return utf8_errc::NoError;
        }

        if (code_unit < 0b1111'0000u)
        {
            if (3 > last - first)
            {
                out = end_of_file;
                first = last;
                return utf8_errc::UnexpectedEof;
            }

            utf8_errc out_error;
            if (bad_trailing(static_cast<unsigned char>(first[1]), out_error) ||
                bad_trailing(static_cast<unsigned char>(first[2]), out_error))
            {
                out = end_of_file;
                first = last;
                return out_error;
            }

            // clang-format off
            out = ((code_unit & 0b0000'1111) << 12)
                | ((static_cast<unsigned char>(first[1]) & 0b0011'1111u) << 6)
                |  (static_cast<unsigned char>(first[2]) & 0b0011'1111u);
            // clang-format on
            first += 3;
            return utf8_errc::NoError;
        }

        if (code_unit < 0b1111'1000u)
        {
            if (4 > last - first)
            {
                out = end_of_file;
                first = last;
                return utf8_errc::UnexpectedEof;
            }

            utf8_errc out_error;
            if (bad_trailing(static_cast<unsigned char>(first[1]), out_error) ||
                bad_trailing(static_cast<unsigned char>(first[2]), out_error) ||
                bad_trailing(static_cast<unsigned char>(first[3]), out_error))
            {
                out = end_of_file;
                first = last;
                return out_error;
            }

            // clang-format off
            out = ((code_unit & 0b0000'0111) << 18)
                | ((static_cast<unsigned char>(first[1]) & 0b0011'1111u) << 12)
                | ((static_cast<unsigned char>(first[2]) & 0b0011'1111u) << 6)
                |  (static_cast<unsigned char>(first[3]) & 0b0011'1111u);
            // clang-format on

            if (out > 0x10'FFFF)
            {
                out = end_of_file;
                first = last;
                return utf8_errc::InvalidCodePoint;
            }

            first += 4;
            return utf8_errc::NoError;
        }

        out = end_of_file;
        first = last;
        return utf8_errc::InvalidCodeUnit;
    }

    bool utf8_is_valid_string(const char* first, const char* last) noexcept
    {
        utf8_errc err;
        Utf8Decoder dec(first, last, err);
        while (!dec.is_eof())
        {
            err = dec.next();
        }

        return err == utf8_errc::NoError;
    }

    char32_t utf16_surrogates_to_code_point(char32_t leading, char32_t trailing) noexcept
    {
        vcpkg::Checks::check_exit(VCPKG_LINE_INFO, utf16_is_leading_surrogate_code_point(leading));
        vcpkg::Checks::check_exit(VCPKG_LINE_INFO, utf16_is_trailing_surrogate_code_point(trailing));

        char32_t res = (leading & 0b11'1111'1111) << 10;
        res |= trailing & 0b11'1111'1111;
        res += 0x0001'0000;

        return res;
    }

    static LocalizedString message(utf8_errc condition)
    {
        switch (condition)
        {
            case utf8_errc::NoError: return msg::format(msgNoError);
            case utf8_errc::InvalidCodeUnit: return msg::format(msgInvalidCodeUnit);
            case utf8_errc::InvalidCodePoint: return msg::format(msgInvalidCodePoint).append_raw(" (>0x10FFFF)");
            case utf8_errc::PairedSurrogates: return msg::format(msgPairedSurrogatesAreInvalid);
            case utf8_errc::UnexpectedContinue: return msg::format(msgContinueCodeUnitInStart);
            case utf8_errc::UnexpectedStart: return msg::format(msgStartCodeUnitInContinue);
            case utf8_errc::UnexpectedEof: return msg::format(msgEndOfStringInCodeUnit);
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    utf8_errc Utf8Decoder::next() noexcept
    {
        if (is_eof())
        {
            // incremented Utf8Decoder at the end of the string
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        const auto old_next = next_;
        const auto last = last_;
        if (old_next == last)
        {
            current_ = end_of_file;
            pointer_to_current_ = last;
            return utf8_errc::NoError;
        }

        char32_t code_point;
        auto err = utf8_decode_code_point(next_, last, code_point);
        if (err != utf8_errc::NoError)
        {
            current_ = end_of_file;
            pointer_to_current_ = last;
            return err;
        }

        if (utf16_is_trailing_surrogate_code_point(code_point) && utf16_is_leading_surrogate_code_point(current_))
        {
            current_ = end_of_file;
            pointer_to_current_ = last;
            next_ = last;
            return utf8_errc::PairedSurrogates;
        }

        current_ = code_point;
        pointer_to_current_ = old_next;
        return utf8_errc::NoError;
    }

    Utf8Decoder& Utf8Decoder::operator++() noexcept
    {
        const auto err = next();
        if (err != utf8_errc::NoError)
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                        msg::format(msgUtf8ConversionFailed).append_raw(": ").append(message(err)));
        }

        return *this;
    }

    Utf8Decoder& Utf8Decoder::operator=(sentinel) noexcept
    {
        current_ = end_of_file;
        pointer_to_current_ = last_;
        next_ = last_;
        return *this;
    }
}
