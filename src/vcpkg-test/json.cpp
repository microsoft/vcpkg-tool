#include <vcpkg-test/util.h>

#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>

#include <iostream>

// TODO: remove this once we switch to C++20 completely
// This is the worst, but we also can't really deal with it any other way.
#if __cpp_char8_t
template<size_t Sz>
static auto u8_string_to_char_string(const char8_t (&literal)[Sz]) -> const char (&)[Sz]
{
    return reinterpret_cast<const char (&)[Sz]>(literal);
}

#define U8_STR(s) (u8_string_to_char_string(u8"" s))
#else
#define U8_STR(s) (u8"" s)
#endif

using namespace vcpkg;
using Json::Value;

TEST_CASE ("JSON stringify weird strings", "[json]")
{
    std::string str = U8_STR("😀 😁 😂 🤣 😃 😄 😅 😆 😉");
    REQUIRE(Json::stringify(Value::string(str)) == ('"' + str + "\"\n"));
    REQUIRE(Json::stringify(Value::string("\xED\xA0\x80")) == "\"\\ud800\"\n"); // unpaired surrogate
}

TEST_CASE ("JSON parse keywords", "[json]")
{
    auto res = Json::parse("true", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_boolean());
    REQUIRE(res.get()->value.boolean(VCPKG_LINE_INFO));
    res = Json::parse(" false ", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_boolean());
    REQUIRE(!res.get()->value.boolean(VCPKG_LINE_INFO));
    res = Json::parse(" null\t ", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_null());
}

TEST_CASE ("JSON parse strings", "[json]")
{
    auto res = Json::parse(R"("")", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO).size() == 0);

    res = Json::parse(R"("\ud800")", "test"); // unpaired surrogate
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == "\xED\xA0\x80");

    const auto make_json_string = [](StringView sv) { return '"' + sv.to_string() + '"'; };
    const StringView radical = U8_STR("⎷");
    const StringView grin = U8_STR("😁");

    res = Json::parse(R"("\uD83D\uDE01")", "test"); // paired surrogates for grin
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == grin.to_string());

    res = Json::parse(make_json_string(radical), "test"); // character in BMP
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == radical);

    res = Json::parse(make_json_string(grin), "test"); // character above BMP
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == grin);
}

TEST_CASE ("JSON parse strings with escapes", "[json]")
{
    auto res = Json::parse(R"("\t")", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == "\t");

    res = Json::parse(R"("\\")", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == "\\");

    res = Json::parse(R"("\/")", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == "/");

    res = Json::parse(R"("\b")", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == "\b");

    res = Json::parse(R"("\f")", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == "\f");

    res = Json::parse(R"("\n")", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == "\n");

    res = Json::parse(R"("\r")", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == "\r");

    res = Json::parse(R"("This is a \"test\", hopefully it worked")", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_string());
    REQUIRE(res.get()->value.string(VCPKG_LINE_INFO) == R"(This is a "test", hopefully it worked)");
}

TEST_CASE ("JSON parse integers", "[json]")
{
    auto res = Json::parse("0", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_integer());
    REQUIRE(res.get()->value.integer(VCPKG_LINE_INFO) == 0);
    res = Json::parse("12345", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_integer());
    REQUIRE(res.get()->value.integer(VCPKG_LINE_INFO) == 12345);
    res = Json::parse("-12345", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_integer());
    REQUIRE(res.get()->value.integer(VCPKG_LINE_INFO) == -12345);
    res = Json::parse("9223372036854775807", "test"); // INT64_MAX
    REQUIRE(res);
    REQUIRE(res.get()->value.is_integer());
    REQUIRE(res.get()->value.integer(VCPKG_LINE_INFO) == 9223372036854775807);
    res = Json::parse("-9223372036854775808", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_integer());
    REQUIRE(res.get()->value.integer(VCPKG_LINE_INFO) == (-9223372036854775807 - 1)); // INT64_MIN (C++'s parser is fun)
}

TEST_CASE ("JSON parse floats", "[json]")
{
    auto res = Json::parse("0.0", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_number());
    REQUIRE(!res.get()->value.is_integer());
    REQUIRE(res.get()->value.number(VCPKG_LINE_INFO) == 0.0);
    REQUIRE(!signbit(res.get()->value.number(VCPKG_LINE_INFO)));
    res = Json::parse("-0.0", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_number());
    REQUIRE(res.get()->value.number(VCPKG_LINE_INFO) == 0.0);
    REQUIRE(signbit(res.get()->value.number(VCPKG_LINE_INFO)));
    res = Json::parse("12345.6789", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_number());
    REQUIRE_THAT(res.get()->value.number(VCPKG_LINE_INFO), Catch::WithinULP(12345.6789, 3));
    res = Json::parse("-12345.6789", "test");
    REQUIRE(res);
    REQUIRE(res.get()->value.is_number());
    REQUIRE_THAT(res.get()->value.number(VCPKG_LINE_INFO), Catch::WithinULP(-12345.6789, 3));
}

TEST_CASE ("JSON parse arrays", "[json]")
{
    auto res = Json::parse("[]", "test");
    REQUIRE(res);
    auto val = std::move(res.get()->value);
    REQUIRE(val.is_array());
    REQUIRE(val.array(VCPKG_LINE_INFO).size() == 0);

    res = Json::parse("[123]", "test");
    REQUIRE(res);
    val = std::move(res.get()->value);
    REQUIRE(val.is_array());
    REQUIRE(val.array(VCPKG_LINE_INFO).size() == 1);
    REQUIRE(val.array(VCPKG_LINE_INFO)[0].is_integer());
    REQUIRE(val.array(VCPKG_LINE_INFO)[0].integer(VCPKG_LINE_INFO) == 123);

    res = Json::parse("[123, 456]", "test");
    REQUIRE(res);
    val = std::move(res.get()->value);
    REQUIRE(val.is_array());
    REQUIRE(val.array(VCPKG_LINE_INFO).size() == 2);
    REQUIRE(val.array(VCPKG_LINE_INFO)[0].is_integer());
    REQUIRE(val.array(VCPKG_LINE_INFO)[0].integer(VCPKG_LINE_INFO) == 123);
    REQUIRE(val.array(VCPKG_LINE_INFO)[1].is_integer());
    REQUIRE(val.array(VCPKG_LINE_INFO)[1].integer(VCPKG_LINE_INFO) == 456);

    res = Json::parse("[123, 456, [null]]", "test");
    REQUIRE(res);
    val = std::move(res.get()->value);
    REQUIRE(val.is_array());
    REQUIRE(val.array(VCPKG_LINE_INFO).size() == 3);
    REQUIRE(val.array(VCPKG_LINE_INFO)[2].is_array());
    REQUIRE(val.array(VCPKG_LINE_INFO)[2].array(VCPKG_LINE_INFO).size() == 1);
    REQUIRE(val.array(VCPKG_LINE_INFO)[2].array(VCPKG_LINE_INFO)[0].is_null());
}

TEST_CASE ("JSON parse objects", "[json]")
{
    auto res = Json::parse("{}", "test");
    REQUIRE(res);
    auto val = std::move(res.get()->value);
    REQUIRE(val.is_object());
    REQUIRE(val.object(VCPKG_LINE_INFO).size() == 0);
}

TEST_CASE ("JSON parse full file", "[json]")
{
    StringView json =
#include "large-json-document.json.inc"
        ;

    auto res = Json::parse(json, "test");
    if (!res)
    {
        std::cerr << res.error() << '\n';
    }
    REQUIRE(res);
}

TEST_CASE ("JSON track newlines", "[json]")
{
    auto res = Json::parse("{\n,", "filename");
    REQUIRE(!res);
    REQUIRE(res.error() ==
            LocalizedString::from_raw(R"(filename:2:1: error: Unexpected character; expected property name
  on expression: ,
                 ^)"));
}

TEST_CASE ("JSON duplicated object keys", "[json]")
{
    auto res = Json::parse("{\"name\": 1, \"name\": 2}", "filename");
    REQUIRE(!res);
    REQUIRE(res.error() == LocalizedString::from_raw(R"(filename:1:13: error: Duplicated key "name" in an object
  on expression: {"name": 1, "name": 2}
                             ^)"));
}

TEST_CASE ("JSON support unicode characters in errors", "[json]")
{
    // unicode characters w/ bytes >1
    auto res = Json::parse(R"json("Δx/Δt" "")json", "filename");
    REQUIRE(!res);
    CHECK(res.error() == LocalizedString::from_raw(R"(filename:1:9: error: Unexpected character; expected EOF
  on expression: "Δx/Δt" ""
                         ^)"));

    // full width unicode characters
    // note that the A is full width
    res = Json::parse(R"json("姐姐aＡ" "")json", "filename");
    REQUIRE(!res);
    CHECK(res.error() == LocalizedString::from_raw(R"(filename:1:8: error: Unexpected character; expected EOF
  on expression: "姐姐aＡ" ""
                           ^)"));

    // incorrect errors in the face of combining characters
    // (this test should be fixed once the underlying bug is fixed)
    res = Json::parse(R"json("é" "")json", "filename");
    REQUIRE(!res);
    CHECK(res.error() == LocalizedString::from_raw(R"(filename:1:6: error: Unexpected character; expected EOF
  on expression: "é" ""
                      ^)"));
}
