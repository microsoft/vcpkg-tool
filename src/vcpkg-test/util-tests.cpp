#include <catch2/catch.hpp>

#include <vcpkg/base/util.h>

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
