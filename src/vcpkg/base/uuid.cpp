#include <vcpkg/base/uuid.h>

#include <random>

using namespace vcpkg;

namespace
{
    struct append_hexits
    {
        constexpr static char hex[17] = "0123456789abcdef";
        void operator()(std::string& res, std::uint8_t bits) const
        {
            res.push_back(hex[(bits >> 4) & 0x0F]);
            res.push_back(hex[(bits >> 0) & 0x0F]);
        }
    };

    // note: this ignores the bits of these numbers that would be where format and variant go
    std::string uuid_of_integers(uint64_t top, uint64_t bottom)
    {
        // uuid_field_size in bytes, not hex characters
        constexpr size_t uuid_top_field_size[] = {4, 2, 2};
        constexpr size_t uuid_bottom_field_size[] = {2, 6};

        // uuid_field_size in hex characters, not bytes
        constexpr size_t uuid_size = 8 + 1 + 4 + 1 + 4 + 1 + 4 + 1 + 12;

        constexpr static append_hexits write_byte;

        // set the version bits to 4
        top &= 0xFFFF'FFFF'FFFF'0FFFULL;
        top |= 0x0000'0000'0000'4000ULL;

        // set the variant bits to 2 (variant one)
        bottom &= 0x3FFF'FFFF'FFFF'FFFFULL;
        bottom |= 0x8000'0000'0000'0000ULL;

        std::string res;
        res.reserve(uuid_size);

        bool first = true;
        size_t start_byte = 0;
        for (auto field_size : uuid_top_field_size)
        {
            if (!first)
            {
                res.push_back('-');
            }
            first = false;
            for (size_t i = start_byte; i < start_byte + field_size; ++i)
            {
                auto shift = 64 - (i + 1) * 8;
                write_byte(res, (top >> shift) & 0xFF);
            }
            start_byte += field_size;
        }

        start_byte = 0;
        for (auto field_size : uuid_bottom_field_size)
        {
            res.push_back('-');
            for (size_t i = start_byte; i < start_byte + field_size; ++i)
            {
                auto shift = 64 - (i + 1) * 8;
                write_byte(res, (bottom >> shift) & 0xFF);
            }
            start_byte += field_size;
        }

        return res;
    }
}

std::string vcpkg::generate_random_UUID()
{
    std::random_device rnd{};
    std::uniform_int_distribution<std::uint64_t> uid{};
    return uuid_of_integers(uid(rnd), uid(rnd));
}
