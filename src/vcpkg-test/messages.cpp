#include <catch2/catch.hpp>

#include <vcpkg/base/json.h>
#include <vcpkg/base/setup-messages.h>

#include <vcpkg/commands.generate-message-map.h>

using namespace vcpkg;
using namespace vcpkg::Commands;

TEST_CASE ("append floaging list", "[LocalizedString]")
{
    const auto a = LocalizedString::from_raw("a");
    const auto b = LocalizedString::from_raw("b");
    CHECK(LocalizedString().append_floating_list(2, std::vector<LocalizedString>{}) == LocalizedString());
    CHECK(LocalizedString().append_floating_list(2, std::vector<LocalizedString>{a}) ==
          LocalizedString::from_raw(" a"));
    const auto expected = LocalizedString::from_raw("    heading\n        a\n        b");
    CHECK(LocalizedString::from_raw("    heading").append_floating_list(2, std::vector<LocalizedString>{a, b}) ==
          expected);
}

TEST_CASE ("get path to locale from LCID", "[messages]")
{
    // valid LCID; Chinese
    auto res = msg::get_locale_path(2052);
    CHECK(res == "locales/messages.zh-Hans.json");

    // invalid LCID
    CHECK(!msg::get_locale_path(0000).has_value());
}
TEST_CASE ("get message_map from LCID", "[messages]")
{
    StringView msg_name = "AddCommandFirstArg";

    // valid lcid; Spanish
    auto map = msg::get_message_map_from_lcid(3082);
    auto msg = map.value_or_exit(VCPKG_LINE_INFO).map.get(msg_name);
    CHECK(msg->string(VCPKG_LINE_INFO) ==
          "El primer par\u00e1metro que se va a agregar debe ser \"artefacto\" o \"puerto\".");
}

TEST_CASE ("generate message get_all_format_args", "[messages]")
{
    LocalizedString err;
    auto res = get_all_format_args("hey ho let's go", err);
    CHECK(err.data() == "");
    CHECK(res == std::vector<StringView>{});

    res = get_all_format_args("hey {ho} let's {go}", err);
    CHECK(err.data() == "");
    CHECK(res == std::vector<StringView>{"ho", "go"});

    res = get_all_format_args("{{ {hey} }}", err);
    CHECK(err.data() == "");
    CHECK(res == std::vector<StringView>{"hey"});

    res = get_all_format_args("{", err);
    CHECK(err.data() == "unbalanced brace in format string \"{\"");
    CHECK(res == std::vector<StringView>{});

    res = get_all_format_args("{ {blah}", err);
    CHECK(err.data() == "unbalanced brace in format string \"{ {blah}\"");
    CHECK(res == std::vector<StringView>{"blah"});

    res = get_all_format_args("{ { {blah} {bloop}", err);
    CHECK(err.data() == "unbalanced brace in format string \"{ { {blah} {bloop}\"");
    CHECK(res == std::vector<StringView>{"blah", "bloop"});
}

TEST_CASE ("generate message get_format_arg_mismatches", "[messages]")
{
    LocalizedString err;
    auto res = get_format_arg_mismatches("hey ho", "", err);
    CHECK(err.data() == "");
    CHECK(res.arguments_without_comment == std::vector<StringView>{});
    CHECK(res.comments_without_argument == std::vector<StringView>{});

    res = get_format_arg_mismatches("hey {ho} let's {go}", "{ho} {go}", err);
    CHECK(err.data() == "");
    CHECK(res.arguments_without_comment == std::vector<StringView>{});
    CHECK(res.comments_without_argument == std::vector<StringView>{});

    res = get_format_arg_mismatches("hey {ho} let's {go}", "invalid format string { {ho} {go}", err);
    CHECK(err.data() == "");
    CHECK(res.arguments_without_comment == std::vector<StringView>{});
    CHECK(res.comments_without_argument == std::vector<StringView>{});

    res = get_format_arg_mismatches("hey { {ho} let's {go}", "{blah}", err);
    CHECK(err.data() == "unbalanced brace in format string \"hey { {ho} let's {go}\"");
    CHECK(res.arguments_without_comment == std::vector<StringView>{});
    CHECK(res.comments_without_argument == std::vector<StringView>{});

    res = get_format_arg_mismatches("hey {ho} let's {go}", "{blah}", err);
    CHECK(err.data() == "");
    CHECK(res.arguments_without_comment == std::vector<StringView>{"go", "ho"});
    CHECK(res.comments_without_argument == std::vector<StringView>{"blah"});

    res = get_format_arg_mismatches("hey {ho} {go} let's {go}", "{blah} {blah}", err);
    CHECK(err.data() == "");
    CHECK(res.arguments_without_comment == std::vector<StringView>{"go", "ho"});
    CHECK(res.comments_without_argument == std::vector<StringView>{"blah"});
}
