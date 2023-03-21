#include <catch2/catch.hpp>

#include <vcpkg/base/api-stable-format.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/span.h>
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

TEST_CASE ("percent encoding", "[strings]")
{
    std::string ascii(127, 0);
    std::iota(ascii.begin(), ascii.end(), static_cast<char>(1));
    REQUIRE(vcpkg::Strings::url_encode(ascii) ==
            "%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F%20%21%22%23%"
            "24%25%26%27%28%29%2A%2B%2C-.%2F0123456789%3A%3B%3C%3D%3E%3F%40ABCDEFGHIJKLMNOPQRSTUVWXYZ%5B%5C%5D%5E_%"
            "60abcdefghijklmnopqrstuvwxyz%7B%7C%7D~%7F");
    // U+1F44D THUMBS UP SIGN and U+1F30F EARTH GLOBE ASIA-AUSTRALIA
    REQUIRE(vcpkg::Strings::url_encode("\xF0\x9F\x91\x8d\xf0\x9f\x8c\x8f") == "%F0%9F%91%8D%F0%9F%8C%8F");
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

TEST_CASE ("contains_any_ignoring_c_comments", "[strings]")
{
    using vcpkg::Strings::contains_any_ignoring_c_comments;
    vcpkg::StringView to_find[] = {"abc", "wer"};
    REQUIRE(contains_any_ignoring_c_comments(R"(abc)", to_find));
    REQUIRE(contains_any_ignoring_c_comments(R"("abc")", to_find));
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"("" //abc)", to_find));
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"(/*abc*/ "")", to_find));
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"(/**abc*/ "")", to_find));
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"(/**abc**/ "")", to_find));
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"(/*abc)", to_find));
    // note that the line end is escaped making the single line comment include the abc
    REQUIRE_FALSE(contains_any_ignoring_c_comments("// test \\\nabc", to_find));
    // note that the comment start is in a string literal so it isn't a comment
    REQUIRE(contains_any_ignoring_c_comments("\"//\" test abc", to_find));
    // note that the comment is in a raw string literal so it isn't a comment
    REQUIRE(contains_any_ignoring_c_comments(R"-(R"( // abc )")-", to_find));
    // found after the raw string literal
    REQUIRE(contains_any_ignoring_c_comments(R"-(R"( // )" abc)-", to_find));
    // comment after the raw string literal
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"-(R"( // )" // abc)-", to_find));
    // the above, but with a d_char_sequence for the raw literal
    REQUIRE(contains_any_ignoring_c_comments(R"-(R"hello( // abc )hello")-", to_find));
    REQUIRE(contains_any_ignoring_c_comments(R"-(R"hello( // )hello" abc)-", to_find));
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"-(R"hello( // )hello" // abc)-", to_find));
    // the above, but with a d_char_sequence that is a needle
    REQUIRE(contains_any_ignoring_c_comments(R"-(R"abc( // abc )abc")-", to_find));
    REQUIRE(contains_any_ignoring_c_comments(R"-(R"abc( // )abc" abc)-", to_find));
    REQUIRE(contains_any_ignoring_c_comments(R"-(R"abc( // )abc" // abc)-", to_find));
    // raw literal termination edge cases
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"-(R")-", to_find));    // ends input
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"-(R"h)-", to_find));   // ends input d_char
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"-(R"()-", to_find));   // ends input paren
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"-(R"h()-", to_find));  // ends input paren d_char
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"-(R"())-", to_find));  // ends input close paren
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"-(R"()")-", to_find)); // ends input exactly
    // raw literal termination edge cases (success)
    REQUIRE(contains_any_ignoring_c_comments(R"-(abcR")-", to_find));    // ends input
    REQUIRE(contains_any_ignoring_c_comments(R"-(abcR"h)-", to_find));   // ends input d_char
    REQUIRE(contains_any_ignoring_c_comments(R"-(abcR"()-", to_find));   // ends input paren
    REQUIRE(contains_any_ignoring_c_comments(R"-(abcR"h()-", to_find));  // ends input paren d_char
    REQUIRE(contains_any_ignoring_c_comments(R"-(abcR"())-", to_find));  // ends input close paren
    REQUIRE(contains_any_ignoring_c_comments(R"-(abcR"()")-", to_find)); // ends input exactly

    REQUIRE(contains_any_ignoring_c_comments(R"-(R"()"abc)-", to_find));

    REQUIRE(contains_any_ignoring_c_comments(R"-(R"hello( hello" // abc )")-", to_find));
    REQUIRE(contains_any_ignoring_c_comments(R"(R"-( // abc )-")", to_find));
    REQUIRE_FALSE(contains_any_ignoring_c_comments(R"(R"-( // hello )-" // abc)", to_find));
    REQUIRE(contains_any_ignoring_c_comments(R"(R"-( /* abc */ )-")", to_find));
    REQUIRE(contains_any_ignoring_c_comments(R"(R"-()- /* abc */ )-")", to_find));
    REQUIRE(contains_any_ignoring_c_comments(R"(qwer )", to_find));
    REQUIRE(contains_any_ignoring_c_comments("\"a\" \"g\" // er \n abc)", to_find));
}

TEST_CASE ("contains_any_ignoring_hash_comments", "[strings]")
{
    using vcpkg::Strings::contains_any_ignoring_hash_comments;
    vcpkg::StringView to_find[] = {"abc", "wer"};
    REQUIRE(contains_any_ignoring_hash_comments("abc", to_find));
    REQUIRE(contains_any_ignoring_hash_comments("wer", to_find));
    REQUIRE(contains_any_ignoring_hash_comments("wer # test", to_find));
    REQUIRE(contains_any_ignoring_hash_comments("\n wer # \n test", to_find));
    REQUIRE_FALSE(contains_any_ignoring_hash_comments("# wer", to_find));
    REQUIRE_FALSE(contains_any_ignoring_hash_comments("\n# wer", to_find));
    REQUIRE_FALSE(contains_any_ignoring_hash_comments("\n  # wer\n", to_find));
    REQUIRE_FALSE(contains_any_ignoring_hash_comments("\n test # wer", to_find));
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
