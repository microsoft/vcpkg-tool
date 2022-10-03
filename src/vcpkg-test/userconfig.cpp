#include <catch2/catch.hpp>

#include <vcpkg/userconfig.h>

using namespace vcpkg;

TEST_CASE ("parses empty", "[userconfig]")
{
    auto result = try_parse_user_config("");
    CHECK(result.user_id == "");
    CHECK(result.user_time == "");
    CHECK(result.user_mac == "");
    CHECK(result.last_completed_survey == "");
}

TEST_CASE ("parses partial", "[userconfig]")
{
    auto result = try_parse_user_config("User-Id: hello");
    CHECK(result.user_id == "hello");
    CHECK(result.user_time == "");
    CHECK(result.user_mac == "");
    CHECK(result.last_completed_survey == "");
}

TEST_CASE ("parses multiple paragraphs ", "[userconfig]")
{
    auto result = try_parse_user_config("User-Id: hello\n\n\n"
                                        "User-Since: there\n"
                                        "Mac-Hash: world\n\n\n"
                                        "Survey-Completed: survey\n");

    CHECK(result.user_id == "hello");
    CHECK(result.user_time == "there");
    CHECK(result.user_mac == "world");
    CHECK(result.last_completed_survey == "survey");
}

TEST_CASE ("to string", "[userconfig]")
{
    UserConfig uut;
    CHECK(uut.to_string() == "User-Id: \n"
                             "User-Since: \n"
                             "Mac-Hash: \n"
                             "Survey-Completed: \n");
    uut.user_id = "alpha";
    uut.user_time = "bravo";
    uut.user_mac = "charlie";
    uut.last_completed_survey = "delta";

    CHECK(uut.to_string() == "User-Id: alpha\n"
                             "User-Since: bravo\n"
                             "Mac-Hash: charlie\n"
                             "Survey-Completed: delta\n");
}
