#include <catch2/catch.hpp>

#include <vcpkg/base/messages.h>

#include <vcpkg/commands.generate-message-map.h>

using namespace vcpkg;
using namespace vcpkg::Commands;

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
