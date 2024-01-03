#include <vcpkg/base/checks.h>
#include <vcpkg/base/unicode.h>

namespace vcpkg::Unicode
{
    Utf8CodeUnitKind utf8_code_unit_kind(unsigned char code_unit) noexcept
    {
        if (code_unit < 0b1000'0000)
        {
            return Utf8CodeUnitKind::StartOne;
        }
        else if (code_unit < 0b1100'0000)
        {
            return Utf8CodeUnitKind::Continue;
        }
        else if (code_unit < 0b1110'0000)
        {
            return Utf8CodeUnitKind::StartTwo;
        }
        else if (code_unit < 0b1111'0000)
        {
            return Utf8CodeUnitKind::StartThree;
        }
        else if (code_unit < 0b1111'1000)
        {
            return Utf8CodeUnitKind::StartFour;
        }
        else
        {
            return Utf8CodeUnitKind::Invalid;
        }
    }

    static constexpr int utf8_code_unit_count(Utf8CodeUnitKind kind) noexcept { return static_cast<int>(kind); }

    int utf8_code_unit_count(char code_unit) noexcept
    {
        return utf8_code_unit_count(utf8_code_unit_kind(static_cast<unsigned char>(code_unit)));
    }

    static constexpr int utf8_encode_code_unit_count(char32_t code_point) noexcept
    {
        if (code_point < 0x80)
        {
            return 1;
        }
        else if (code_point < 0x800)
        {
            return 2;
        }
        else if (code_point < 0x10000)
        {
            return 3;
        }
        else if (code_point < 0x110000)
        {
            return 4;
        }
        else
        {
            vcpkg::Checks::msg_exit_with_message(
                VCPKG_LINE_INFO,
                msg::format(msgInvalidCodePoint).append_raw(fmt::format("({:x})", static_cast<uint32_t>(code_point))));
        }
    }

    int utf8_encode_code_point(char (&array)[4], char32_t code_point) noexcept
    {
        // count \in {2, 3, 4}
        const auto start_code_point = [](char32_t code_point, int count) {
            const unsigned char and_mask = 0xFF >> (count + 1);
            const unsigned char or_mask = (0xFF << (8 - count)) & 0xFF;
            const int shift = 6 * (count - 1);
            return static_cast<char>(or_mask | ((code_point >> shift) & and_mask));
        };
        // count \in {2, 3, 4}, byte \in {1, 2, 3}
        const auto continue_code_point = [](char32_t code_point, int count, int byte) {
            constexpr unsigned char and_mask = 0xFF >> 2;
            constexpr unsigned char or_mask = (0xFF << 7) & 0xFF;
            const int shift = 6 * (count - byte - 1);
            return static_cast<char>(or_mask | ((code_point >> shift) & and_mask));
        };

        int count = utf8_encode_code_unit_count(code_point);
        if (count == 1)
        {
            array[0] = static_cast<char>(code_point);
            return 1;
        }

        array[0] = start_code_point(code_point, count);
        for (int i = 1; i < count; ++i)
        {
            array[i] = continue_code_point(code_point, count, i);
        }

        return count;
    }

    std::pair<const char*, utf8_errc> utf8_decode_code_point(const char* first,
                                                             const char* last,
                                                             char32_t& out) noexcept
    {
        out = end_of_file;
        if (first == last)
        {
            return {last, utf8_errc::NoError};
        }

        auto code_unit = *first;
        auto kind = utf8_code_unit_kind(static_cast<unsigned char>(code_unit));
        const int count = utf8_code_unit_count(kind);

        const char* it = first + 1;

        if (kind == Utf8CodeUnitKind::Invalid)
        {
            return {it, utf8_errc::InvalidCodeUnit};
        }
        else if (kind == Utf8CodeUnitKind::Continue)
        {
            return {it, utf8_errc::UnexpectedContinue};
        }
        else if (count > last - first)
        {
            return {last, utf8_errc::UnexpectedEof};
        }

        if (count == 1)
        {
            out = static_cast<char32_t>(code_unit);
            return {it, utf8_errc::NoError};
        }

        // 2 -> 0b0001'1111, 6
        // 3 -> 0b0000'1111, 12
        // 4 -> 0b0000'0111, 18
        const auto start_mask = static_cast<unsigned char>(0xFF >> (count + 1));
        const int start_shift = 6 * (count - 1);
        char32_t code_point = static_cast<char32_t>(code_unit & start_mask) << start_shift;

        constexpr unsigned char continue_mask = 0b0011'1111;
        for (int byte = 1; byte < count; ++byte)
        {
            code_unit = *it++;

            kind = utf8_code_unit_kind(code_unit);
            if (kind == Utf8CodeUnitKind::Invalid)
            {
                return {it, utf8_errc::InvalidCodeUnit};
            }
            else if (kind != Utf8CodeUnitKind::Continue)
            {
                return {it, utf8_errc::UnexpectedStart};
            }

            const int shift = 6 * (count - byte - 1);
            code_point |= (code_unit & continue_mask) << shift;
        }

        if (code_point > 0x10'FFFF)
        {
            return {it, utf8_errc::InvalidCodePoint};
        }

        out = code_point;
        return {it, utf8_errc::NoError};
    }

    bool utf8_is_valid_string(const char* first, const char* last) noexcept
    {
        utf8_errc err = utf8_errc::NoError;
        for (auto dec = Utf8Decoder(first, last); dec != dec.end(); err = dec.next())
        {
        }
        return err == utf8_errc::NoError;
    }

    char32_t utf16_surrogates_to_code_point(char32_t leading, char32_t trailing)
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

    char const* Utf8Decoder::pointer_to_current() const noexcept
    {
        if (is_eof())
        {
            return last_;
        }

        auto count = utf8_encode_code_unit_count(current_);
        return next_ - count;
    }

    utf8_errc Utf8Decoder::next()
    {
        if (is_eof())
        {
            // incremented Utf8Decoder at the end of the string
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        if (next_ == last_)
        {
            current_ = end_of_file;
            return utf8_errc::NoError;
        }

        char32_t code_point;
        auto new_next = utf8_decode_code_point(next_, last_, code_point);
        if (new_next.second != utf8_errc::NoError)
        {
            *this = sentinel();
            return new_next.second;
        }

        if (utf16_is_trailing_surrogate_code_point(code_point) && utf16_is_leading_surrogate_code_point(current_))
        {
            *this = sentinel();
            return utf8_errc::PairedSurrogates;
        }

        next_ = new_next.first;
        current_ = code_point;
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
        next_ = last_;
        current_ = end_of_file;
        return *this;
    }
}
