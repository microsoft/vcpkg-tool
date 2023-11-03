#include <vcpkg-test/util.h>

#include <vcpkg/base/util.h>

#include <iterator>
#include <vector>

namespace Util = vcpkg::Util;

TEST_CASE ("find_nth", "[util]")
{
    std::vector<int> v;

    CHECK(Util::find_nth(v, 1, 0) == v.end());

    v.insert(v.end(), {1, 2, 1, 3, 1, 4});

    CHECK(Util::find_nth(v, 1, 0) == v.begin());
    CHECK(Util::find_nth(v, 2, 0) == v.begin() + 1);
    CHECK(Util::find_nth(v, 5, 0) == v.end());
    CHECK(Util::find_nth(v, 2, 1) == v.end());

    CHECK(Util::find_nth(v, 1, 1) == v.begin() + 2);
    CHECK(Util::find_nth(v, 1, 2) == v.begin() + 4);
    CHECK(Util::find_nth(v, 1, 3) == v.end());
}

TEST_CASE ("find_nth_from_last", "[util]")
{
    std::vector<int> v;

    CHECK(Util::find_nth_from_last(v, 1, 0) == v.end());

    v.insert(v.end(), {1, 2, 1, 3, 1, 4});

    CHECK(Util::find_nth_from_last(v, 1, 0) == v.begin() + 4);
    CHECK(Util::find_nth_from_last(v, 2, 0) == v.begin() + 1);
    CHECK(Util::find_nth_from_last(v, 5, 0) == v.end());
    CHECK(Util::find_nth_from_last(v, 2, 1) == v.end());

    CHECK(Util::find_nth_from_last(v, 1, 1) == v.begin() + 2);
    CHECK(Util::find_nth_from_last(v, 1, 2) == v.begin());
    CHECK(Util::find_nth_from_last(v, 1, 3) == v.end());
}

static void set_duplicates_test_case(std::vector<int> input, std::vector<int> expected)
{
    std::vector<int> results;
    Util::set_duplicates(input.begin(), input.end(), std::back_inserter(results));
    CHECK(expected == results);
}

TEST_CASE ("set_duplicates", "[util]")
{
    // empty
    set_duplicates_test_case({}, {});
    // one
    set_duplicates_test_case({1}, {});
    // no duplicates
    set_duplicates_test_case({1, 2, 3, 4}, {});
    // duplicate at the beginning
    set_duplicates_test_case({1, 1, 2, 3, 4}, {1});
    set_duplicates_test_case({1, 1, 1, 2, 3, 4}, {1});
    // duplicate in the middle
    set_duplicates_test_case({1, 2, 2, 3, 4}, {2});
    set_duplicates_test_case({1, 2, 2, 2, 3, 4}, {2});
    // duplicate at the end
    set_duplicates_test_case({1, 2, 3, 4, 4}, {4});
    set_duplicates_test_case({1, 2, 3, 4, 4, 4}, {4});
    // multiple duplicates
    set_duplicates_test_case({1, 1, 2, 3, 4, 4}, {1, 4});
}

TEST_CASE ("append", "[util]")
{
    {
        std::vector<std::string> a;
        a.emplace_back("abc");
        a.emplace_back("def");
        a.emplace_back("ghi");
        std::vector<std::string> b;
        b.emplace_back("jkl");
        b.emplace_back("mno");
        b.emplace_back("pqr");
        vcpkg::Util::Vectors::append(&a, std::move(b));
        REQUIRE(b.size() == 3);
        REQUIRE(b[0] == "");
        REQUIRE(a.size() == 6);
    }
    {
        std::vector<std::string> a;
        a.emplace_back("abc");
        a.emplace_back("def");
        a.emplace_back("ghi");
        std::vector<std::string> b;
        b.emplace_back("jkl");
        b.emplace_back("mno");
        b.emplace_back("pqr");
        vcpkg::Util::Vectors::append(&a, b);
        REQUIRE(b.size() == 3);
        REQUIRE(b[0] == "jkl");
        REQUIRE(a.size() == 6);
    }
    {
        std::vector<std::string> a;
        a.emplace_back("abc");
        a.emplace_back("def");
        a.emplace_back("ghi");
        const std::vector<std::string> b{
            "jkl",
            "mno",
            "pqr",
        };
        vcpkg::Util::Vectors::append(&a, b);
        REQUIRE(b.size() == 3);
        REQUIRE(b[0] == "jkl");
        REQUIRE(a.size() == 6);
    }
}
