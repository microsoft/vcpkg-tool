#include <vcpkg-test/util.h>

#include <vcpkg/base/unicode.h>

#include <iterator>

using namespace vcpkg::Unicode;

TEST_CASE ("Utf8Decoder valid", "[unicode]")
{
    const char32_t* expected = U"";
    const char* input = "";
    SECTION ("hello")
    {
        expected = U"hello";
        input = "hello";
    }

    SECTION ("all types of code points")
    {
        expected = U"one: a two: \u00E9 three: \u672C four: \U0001F3C8";
        input = "one: a two: \xC3\xA9 three: \xE6\x9C\xAC four: \xF0\x9F\x8F\x88";
    }

    SECTION ("wtf-8 leading")
    {
        // U+1F3C8 as WTF-8
        static constexpr char32_t storage[] = {0xD83C, 0};
        expected = storage;
        input = "\xED\xA0\xBC";
    }

    SECTION ("wtf-8 trailing")
    {
        // U+1F3C8 as WTF-8
        static constexpr char32_t storage[] = {0xDFC8, 0};
        expected = storage;
        input = "\xED\xBF\x88";
    }

    auto input_end = input + strlen(input);
    utf8_errc utf8_error;
    Utf8Decoder decode(input, utf8_error);
    REQUIRE(utf8_error == utf8_errc::NoError);
    // strlen for char32_t:
    size_t expected_size = 0;
    for (auto* e = expected; *e; ++e)
    {
        ++expected_size;
    }

    auto decode_at_end = decode;
    for (size_t idx = 0; idx < expected_size; ++idx)
    {
        REQUIRE(!decode_at_end.is_eof());
        REQUIRE(decode_at_end.next() == utf8_errc::NoError);
    }

    for (size_t idx = 0; idx < expected_size; ++idx)
    {
        REQUIRE(static_cast<bool>(decode != decode_at_end)); // compare iterator
        REQUIRE(*decode == expected[idx]);
        REQUIRE(!decode.is_eof());
        char32_t decoded;
        auto pointer_to_current = decode.pointer_to_current();
        const auto original_pointer_to_current = pointer_to_current;
        REQUIRE(utf8_decode_code_point(pointer_to_current, input_end, decoded) == utf8_errc::NoError);
        REQUIRE(decoded == expected[idx]);
        std::string encoded;
        REQUIRE(utf8_append_code_point(encoded, decoded));
        REQUIRE(std::equal(encoded.begin(), encoded.end(), original_pointer_to_current));
        REQUIRE(decode.next() == utf8_errc::NoError);
    }

    REQUIRE(static_cast<bool>(decode == decode_at_end));
}

TEST_CASE ("Utf8Decoder first decode empty", "[unicode]")
{
    utf8_errc err;
    Utf8Decoder uut("", err);
    REQUIRE(err == utf8_errc::NoError);
    REQUIRE(uut.is_eof());
    REQUIRE(static_cast<bool>(uut == uut));
}

TEST_CASE ("Utf8Decoder invalid", "[unicode]")
{
    utf8_errc err;
    // clang-format off
    Utf8Decoder uut(GENERATE(
        "hello \xFF too big",
        "hello \xC3\xBF\xBF\xBF also too big",
        "hello \x9C continuation",
        "hello \xE0\x28 overlong",
        "hello \xED\xA0\xBC\xED\xBF\x88 paired WTF-8",
        "missing two: \xC3",
        "missing three one: \xE6\x9C",
        "missing three two: \xE6",
        "missing four one: \xF0\x9F\x8F",
        "missing four two: \xF0\x9F",
        "missing four three: \xF0"
    ), err);
    // clang-format on
    while (err == utf8_errc::NoError)
    {
        REQUIRE(!uut.is_eof());
        err = uut.next();
    }

    REQUIRE(uut.is_eof());
}

TEST_CASE ("Utf8Decoder empty current", "[unicode]")
{
    char storage[] = "";
    utf8_errc err;
    Utf8Decoder uut(storage, err);
    REQUIRE(err == utf8_errc::NoError);
    REQUIRE(uut.pointer_to_current() == storage);
    REQUIRE(uut.is_eof());
}

TEST_CASE ("utf8_is_valid_string fails", "[unicode]")
{
    const char* test = GENERATE("hello \xFF too big",
                                "hello \xC3\xBF\xBF\xBF also too big",
                                "hello \x9C continuation",
                                "hello \xE0\x28 overlong",
                                "hello \xED\xA0\xBC\xED\xBF\x88 paired WTF-8",
                                "missing two: \xC3",
                                "missing three one: \xE6\x9C",
                                "missing three two: \xE6",
                                "missing four one: \xF0\x9F\x8F",
                                "missing four two: \xF0\x9F",
                                "missing four three: \xF0");
    REQUIRE(!utf8_is_valid_string(test, test + strlen(test)));
}

TEST_CASE ("utf8_is_valid_string fails at end", "[unicode]")
{
    const char* test = GENERATE("\xFF",
                                "\xC3\xBF\xBF\xBF",
                                "\x9C",
                                "\xE0\x28",
                                "\xED\xA0\xBC\xED\xBF\x88",
                                "\xC3",
                                "\xE6\x9C",
                                "\xE6",
                                "\xF0\x9F\x8F",
                                "\xF0\x9F",
                                "\xF0");
    REQUIRE(!utf8_is_valid_string(test, test + strlen(test)));
}