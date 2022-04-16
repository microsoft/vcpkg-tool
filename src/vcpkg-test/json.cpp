#include <catch2/catch.hpp>

#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/unicode.h>

#include <iostream>

#include "math.h"

// TODO: remove this once we switch to C++20 completely
// This is the worst, but we also can't really deal with it any other way.
#if __cpp_char8_t
template<size_t Sz>
static auto _u8_string_to_char_string(const char8_t (&literal)[Sz]) -> const char (&)[Sz]
{
    return reinterpret_cast<const char(&)[Sz]>(literal);
}

#define U8_STR(s) (::vcpkg::Unicode::_u8_string_to_char_string(u8"" s))
#else
#define U8_STR(s) (u8"" s)
#endif

namespace Json = vcpkg::Json;
using Json::Value;

static std::string mystringify(const Value& val) { return Json::stringify(val, Json::JsonStyle{}); }

TEST_CASE ("JSON stringify weird strings", "[json]")
{
    std::string str = U8_STR("😀 😁 😂 🤣 😃 😄 😅 😆 😉");
    REQUIRE(mystringify(Value::string(str)) == ('"' + str + "\"\n"));
    REQUIRE(mystringify(Value::string("\xED\xA0\x80")) == "\"\\ud800\"\n"); // unpaired surrogate
}

TEST_CASE ("JSON parse keywords", "[json]")
{
    {
        auto res = Json::parse("true");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_boolean());
        REQUIRE(res.get()->first.boolean());
    }

    {
        auto res = Json::parse(" false ");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_boolean());
        REQUIRE(!res.get()->first.boolean());
    }

    {
        auto res = Json::parse(" null\t ");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_null());
    }
}

TEST_CASE ("JSON parse strings", "[json]")
{
    {
        auto res = Json::parse(R"("")");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string().size() == 0);
    }

    {
        auto res = Json::parse(R"("\ud800")"); // unpaired surrogate
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == "\xED\xA0\x80");
    }

    const auto make_json_string = [](vcpkg::StringView sv) { return '"' + sv.to_string() + '"'; };
    const vcpkg::StringView radical = U8_STR("⎷");
    const vcpkg::StringView grin = U8_STR("😁");

    {
        auto res = Json::parse(R"("\uD83D\uDE01")"); // paired surrogates for grin
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == grin.to_string());
    }

    {
        auto res = Json::parse(make_json_string(radical)); // character in BMP
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == radical);
    }

    {
        auto res = Json::parse(make_json_string(grin)); // character above BMP
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == grin);
    }
}

TEST_CASE ("JSON parse strings with escapes", "[json]")
{
    {
        auto res = Json::parse(R"("\t")");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == "\t");
    }

    {
        auto res = Json::parse(R"("\\")");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == "\\");
    }

    {
        auto res = Json::parse(R"("\/")");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == "/");
    }

    {
        auto res = Json::parse(R"("\b")");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == "\b");
    }

    {
        auto res = Json::parse(R"("\f")");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == "\f");
    }

    {
        auto res = Json::parse(R"("\n")");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == "\n");
    }

    {
        auto res = Json::parse(R"("\r")");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == "\r");
    }

    {
        auto res = Json::parse(R"("This is a \"test\", hopefully it worked")");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_string());
        REQUIRE(res.get()->first.string() == R"(This is a "test", hopefully it worked)");
    }
}

TEST_CASE ("JSON parse integers", "[json]")
{
    {
        auto res = Json::parse("0");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_integer());
        REQUIRE(res.get()->first.integer() == 0);
    }

    {
        auto res = Json::parse("12345");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_integer());
        REQUIRE(res.get()->first.integer() == 12345);
    }

    {
        auto res = Json::parse("-12345");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_integer());
        REQUIRE(res.get()->first.integer() == -12345);
    }

    {
        auto res = Json::parse("9223372036854775807"); // INT64_MAX
        REQUIRE(res);
        REQUIRE(res.get()->first.is_integer());
        REQUIRE(res.get()->first.integer() == 9223372036854775807);
    }

    {
        auto res = Json::parse("-9223372036854775808");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_integer());
        REQUIRE(res.get()->first.integer() == (-9223372036854775807 - 1)); // INT64_MIN (C++'s parser is fun)
    }
}

TEST_CASE ("JSON parse floats", "[json]")
{
    {
        auto res = Json::parse("0.0");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_number());
        REQUIRE(!res.get()->first.is_integer());
        REQUIRE(res.get()->first.number() == 0.0);
        REQUIRE(!signbit(res.get()->first.number()));
    }

    {
        auto res = Json::parse("-0.0");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_number());
        REQUIRE(res.get()->first.number() == 0.0);
        REQUIRE(signbit(res.get()->first.number()));
    }

    {
        auto res = Json::parse("12345.6789");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_number());
        REQUIRE_THAT(res.get()->first.number(), Catch::WithinULP(12345.6789, 3));
    }

    {
        auto res = Json::parse("-12345.6789");
        REQUIRE(res);
        REQUIRE(res.get()->first.is_number());
        REQUIRE_THAT(res.get()->first.number(), Catch::WithinULP(-12345.6789, 3));
    }
}

TEST_CASE ("JSON parse arrays", "[json]")
{
    {
        auto res = Json::parse("[]");
        REQUIRE(res);
        auto val = std::move(res.get()->first);
        REQUIRE(val.is_array());
        REQUIRE(val.array().size() == 0);
    }

    {
        auto res = Json::parse("[123]");
        REQUIRE(res);
        auto val = std::move(res.get()->first);
        REQUIRE(val.is_array());
        REQUIRE(val.array().size() == 1);
        REQUIRE(val.array()[0].is_integer());
        REQUIRE(val.array()[0].integer() == 123);
    }

    {
        auto res = Json::parse("[123, 456]");
        REQUIRE(res);
        auto val = std::move(res.get()->first);
        REQUIRE(val.is_array());
        REQUIRE(val.array().size() == 2);
        REQUIRE(val.array()[0].is_integer());
        REQUIRE(val.array()[0].integer() == 123);
        REQUIRE(val.array()[1].is_integer());
        REQUIRE(val.array()[1].integer() == 456);
    }

    {
        auto res = Json::parse("[123, 456, [null]]");
        REQUIRE(res);
        auto val = std::move(res.get()->first);
        REQUIRE(val.is_array());
        REQUIRE(val.array().size() == 3);
        REQUIRE(val.array()[2].is_array());
        REQUIRE(val.array()[2].array().size() == 1);
        REQUIRE(val.array()[2].array()[0].is_null());
    }
}

TEST_CASE ("JSON parse objects", "[json]")
{
    auto res = Json::parse("{}");
    REQUIRE(res);
    auto val = std::move(res.get()->first);
    REQUIRE(val.is_object());
    REQUIRE(val.object().size() == 0);
}

TEST_CASE ("JSON parse full file", "[json]")
{
    vcpkg::StringView json =
#include "large-json-document.json.inc"
        ;

    auto res = Json::parse(json);
    if (!res)
    {
        std::cerr << res.error()->format() << '\n';
    }
    REQUIRE(res);
}

TEST_CASE ("JSON track newlines", "[json]")
{
    auto res = Json::parse("{\n,", "filename");
    REQUIRE(!res);
    REQUIRE(res.error()->format() ==
            R"(filename:2:1: error: Unexpected character; expected property name
    on expression: ,
                   ^
)");
}

TEST_CASE ("JSON duplicated object keys", "[json]")
{
    auto res = Json::parse("{\"name\": 1, \"name\": 2}", "filename");
    REQUIRE(!res);
    REQUIRE(res.error()->format() ==
            R"(filename:1:13: error: Duplicated key "name" in an object
    on expression: {"name": 1, "name": 2}
                               ^
)");
}

TEST_CASE ("JSON support unicode characters in errors", "[json]")
{
    {
        // unicode characters w/ bytes >1
        auto res = Json::parse(R"json("Δx/Δt" "")json", "filename");
        REQUIRE(!res);
        CHECK(res.error()->format() ==
              R"(filename:1:9: error: Unexpected character; expected EOF
    on expression: "Δx/Δt" ""
                           ^
)");
    }

    {
        // full width unicode characters
        // note that the A is full width
        auto res = Json::parse(R"json("姐姐aＡ" "")json", "filename");
        REQUIRE(!res);
        CHECK(res.error()->format() ==
              R"(filename:1:8: error: Unexpected character; expected EOF
    on expression: "姐姐aＡ" ""
                             ^
)");
    }

    {
        // incorrect errors in the face of combining characters
        // (this test should be fixed once the underlying bug is fixed)
        auto res = Json::parse(R"json("é" "")json", "filename");
        REQUIRE(!res);
        CHECK(res.error()->format() ==
              R"(filename:1:6: error: Unexpected character; expected EOF
    on expression: "é" ""
                        ^
)");
    }
}
