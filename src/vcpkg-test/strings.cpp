#include <catch2/catch.hpp>

#include <vcpkg/base/api_stable_format.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/strings.h>

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

TEST_CASE ("b32 encoding", "[strings]")
{
    using u64 = uint64_t;

    std::vector<std::pair<u64, std::string>> map;

    map.emplace_back(0, "AAAAAAAAAAAAA");
    map.emplace_back(1, "BAAAAAAAAAAAA");

    map.emplace_back(u64(1) << 32, "AAAAAAEAAAAAA");
    map.emplace_back((u64(1) << 32) + 1, "BAAAAAEAAAAAA");

    map.emplace_back(0xE4D0'1065'D11E'0229, "JRA4RIXMQAUJO");
    map.emplace_back(0xA626'FE45'B135'07FF, "77BKTYWI6XJMK");
    map.emplace_back(0xEE36'D228'0C31'D405, "FAVDDGAFSWN4O");
    map.emplace_back(0x1405'64E7'FE7E'A88C, "MEK5H774ELBIB");
    map.emplace_back(0xFFFF'FFFF'FFFF'FFFF, "777777777777P");

    for (const auto& pr : map)
    {
        REQUIRE(vcpkg::Strings::b32_encode(pr.first) == pr.second);
    }
}

TEST_CASE ("split by char", "[strings]")
{
    using vcpkg::Strings::split;
    using result_t = std::vector<std::string>;
    REQUIRE(split(",,,,,,", ',').empty());
    REQUIRE(split(",,a,,b,,", ',') == result_t{"a", "b"});
    REQUIRE(split("hello world", ' ') == result_t{"hello", "world"});
    REQUIRE(split("    hello  world    ", ' ') == result_t{"hello", "world"});
    REQUIRE(split("no delimiters", ',') == result_t{"no delimiters"});
}

TEST_CASE ("find_first_of", "[strings]")
{
    using vcpkg::Strings::find_first_of;
    REQUIRE(find_first_of("abcdefg", "hij") == std::string());
    REQUIRE(find_first_of("abcdefg", "a") == std::string("abcdefg"));
    REQUIRE(find_first_of("abcdefg", "g") == std::string("g"));
    REQUIRE(find_first_of("abcdefg", "bg") == std::string("bcdefg"));
    REQUIRE(find_first_of("abcdefg", "gb") == std::string("bcdefg"));
}

TEST_CASE ("edit distance", "[strings]")
{
    using vcpkg::Strings::byte_edit_distance;
    REQUIRE(byte_edit_distance("", "") == 0);
    REQUIRE(byte_edit_distance("a", "a") == 0);
    REQUIRE(byte_edit_distance("abcd", "abcd") == 0);
    REQUIRE(byte_edit_distance("aaa", "aa") == 1);
    REQUIRE(byte_edit_distance("aa", "aaa") == 1);
    REQUIRE(byte_edit_distance("abcdef", "bcdefa") == 2);
    REQUIRE(byte_edit_distance("hello", "world") == 4);
    REQUIRE(byte_edit_distance("CAPITAL", "capital") == 7);
    REQUIRE(byte_edit_distance("", "hello") == 5);
    REQUIRE(byte_edit_distance("world", "") == 5);
}

TEST_CASE ("replace_all", "[strings]")
{
    REQUIRE(vcpkg::Strings::replace_all("literal", "ter", "x") == "lixal");
}

TEST_CASE ("inplace_replace_all", "[strings]")
{
    using vcpkg::Strings::inplace_replace_all;
    std::string target;
    inplace_replace_all(target, "", "content");
    REQUIRE(target.empty());
    target = "aa";
    inplace_replace_all(target, "a", "content");
    REQUIRE(target == "contentcontent");
    inplace_replace_all(target, "content", "");
    REQUIRE(target.empty());
    target = "ababababa";
    inplace_replace_all(target, "aba", "X");
    REQUIRE(target == "XbXba");
    target = "ababababa";
    inplace_replace_all(target, "aba", "aba");
    REQUIRE(target == "ababababa");
}

TEST_CASE ("inplace_replace_all(char)", "[strings]")
{
    using vcpkg::Strings::inplace_replace_all;
    static_assert(noexcept(inplace_replace_all(std::declval<std::string&>(), 'a', 'a')));

    std::string target;
    inplace_replace_all(target, ' ', '?');
    REQUIRE(target.empty());
    target = "hello";
    inplace_replace_all(target, 'l', 'w');
    REQUIRE(target == "hewwo");
    inplace_replace_all(target, 'w', 'w');
    REQUIRE(target == "hewwo");
    inplace_replace_all(target, 'x', '?');
    REQUIRE(target == "hewwo");
}

TEST_CASE ("api_stable_format(sv,append_f)", "[strings]")
{
    namespace Strings = vcpkg::Strings;
    using vcpkg::api_stable_format;
    using vcpkg::nullopt;
    using vcpkg::StringView;

    std::string target;
    auto res = api_stable_format("{", [](std::string&, StringView) { CHECK(false); });
    REQUIRE(!res.has_value());
    res = api_stable_format("}", [](std::string&, StringView) { CHECK(false); });
    REQUIRE(!res.has_value());
    res = api_stable_format("{ {", [](std::string&, StringView) { CHECK(false); });
    REQUIRE(!res.has_value());
    res = api_stable_format("{ {}", [](std::string&, StringView) { CHECK(false); });
    REQUIRE(!res.has_value());

    res = api_stable_format("}}", [](std::string&, StringView) { CHECK(false); });
    REQUIRE(*res.get() == "}");
    res = api_stable_format("{{", [](std::string&, StringView) { CHECK(false); });
    REQUIRE(*res.get() == "{");

    res = api_stable_format("{x}{y}{z}", [](std::string& out, StringView t) {
        CHECK((t == "x" || t == "y" || t == "z"));
        Strings::append(out, t, t);
    });
    REQUIRE(*res.get() == "xxyyzz");
    res = api_stable_format("{x}}}", [](std::string& out, StringView t) {
        CHECK(t == "x");
        Strings::append(out, "hello");
    });
    REQUIRE(*res.get() == "hello}");
    res = api_stable_format("123{x}456", [](std::string& out, StringView t) {
        CHECK(t == "x");
        Strings::append(out, "hello");
    });
    REQUIRE(*res.get() == "123hello456");
}
