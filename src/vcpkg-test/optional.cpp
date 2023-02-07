#include <catch2/catch.hpp>

#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/util.h>

#include <vector>

using namespace vcpkg;

namespace
{
    struct identity_projection
    {
        template<class T>
        const T& operator()(const T& val) noexcept
        {
            return val;
        }
    };
}

TEST_CASE ("equal", "[optional]")
{
    CHECK(Optional<int>{} == Optional<int>{});
    CHECK_FALSE(Optional<int>{} == Optional<int>{42});
    CHECK_FALSE(Optional<int>{42} == Optional<int>{});
    CHECK_FALSE(Optional<int>{1729} == Optional<int>{42});
    CHECK(Optional<int>{42} == Optional<int>{42});
}

TEST_CASE ("ref conversion", "[optional]")
{
    Optional<int> i_empty;
    Optional<int> i_1 = 1;
    const Optional<int> ci_1 = 1;

    Optional<int&> ref_empty = i_empty;
    Optional<const int&> cref_empty = i_empty;

    Optional<int&> ref_1 = i_1;
    Optional<const int&> cref_1 = ci_1;

    REQUIRE(ref_empty.has_value() == false);
    REQUIRE(cref_empty.has_value() == false);

    REQUIRE(ref_1.get() == i_1.get());
    REQUIRE(cref_1.get() == ci_1.get());

    ref_empty = i_1;
    cref_empty = ci_1;
    REQUIRE(ref_empty.get() == i_1.get());
    REQUIRE(cref_empty.get() == ci_1.get());

    const int x = 5;
    cref_1 = x;
    REQUIRE(cref_1.get() == &x);
}

TEST_CASE ("value conversion", "[optional]")
{
    Optional<long> j = 1;
    Optional<int> i = j;
    Optional<const char*> cstr = "hello, world!";
    Optional<std::string> cppstr = cstr;

    std::vector<int> v{1, 2, 3};
    Optional<std::vector<int>&> o_v(v);
    REQUIRE(o_v.has_value());
    REQUIRE(o_v.get()->size() == 3);
    Optional<std::vector<int>> o_w(std::move(o_v));
    REQUIRE(o_w.has_value());
    REQUIRE(o_w.get()->size() == 3);
    // Moving from Optional<&> should not move the underlying object
    REQUIRE(o_v.has_value());
    REQUIRE(o_v.get()->size() == 3);
}

TEST_CASE ("optional.map", "[optional]")
{
    const Optional<std::unique_ptr<int>> move_only;

    Optional<int*> m = move_only.map([](auto&& p) { return p.get(); });
    Optional<Optional<int*>> n =
        move_only.map([](auto&& p) -> Optional<int*> { return p ? Optional<int*>{p.get()} : nullopt; });
    Optional<NullOpt> o = move_only.map([](auto&&) { return nullopt; });

    Optional<int> five = 5;

    struct MoveTest
    {
        int operator()(int&&) const { return 1; }
        int operator()(const int&) const { return -1; }
    } move_test;

    Optional<int> dst = std::move(five).map(move_test);
    REQUIRE(dst == 1);
    Optional<int> dst2 = five.map(move_test);
    REQUIRE(dst2 == -1);
}

TEST_CASE ("common_projection", "[optional]")
{
    std::vector<int> input;
    CHECK(!common_projection(input, identity_projection{}).has_value());
    input.push_back(42);
    CHECK(common_projection(input, identity_projection{}).value_or_exit(VCPKG_LINE_INFO) == 42);
    input.push_back(42);
    CHECK(common_projection(input, identity_projection{}).value_or_exit(VCPKG_LINE_INFO) == 42);
    input.push_back(1729);
    CHECK(!common_projection(input, identity_projection{}).has_value());
}

TEST_CASE ("operator==/operator!=", "[optional]")
{
    SECTION ("same type - opt == opt")
    {
        Optional<std::string> s1;
        Optional<std::string> s2;

        // none == none
        CHECK(s1 == s2);
        CHECK_FALSE(s1 != s2);
        CHECK(s2 == s1);
        CHECK_FALSE(s2 != s1);

        // some("") != none
        s1 = "";
        CHECK_FALSE(s1 == s2);
        CHECK(s1 != s2);
        CHECK_FALSE(s2 == s1);
        CHECK(s2 != s1);

        // some("") == some("")
        s2 = "";
        CHECK(s1 == s2);
        CHECK_FALSE(s1 != s2);
        CHECK(s2 == s1);
        CHECK_FALSE(s2 != s1);

        // some("hi") != some("")
        s1 = "hi";
        CHECK_FALSE(s1 == s2);
        CHECK(s1 != s2);
        CHECK_FALSE(s2 == s1);
        CHECK(s2 != s1);
    };

    SECTION ("same type - opt == raw")
    {
        Optional<std::string> opt_string;
        std::string string;

        // none != ""
        CHECK_FALSE(opt_string == string);
        CHECK(opt_string != string);
        CHECK_FALSE(string == opt_string);
        CHECK(string != opt_string);

        // some("") == ""
        opt_string = "";
        CHECK(opt_string == string);
        CHECK_FALSE(opt_string != string);
        CHECK(string == opt_string);
        CHECK_FALSE(string != opt_string);

        // some("hi") != ""
        opt_string = "hi";
        CHECK_FALSE(opt_string == string);
        CHECK(opt_string != string);
        CHECK_FALSE(string == opt_string);
        CHECK(string != opt_string);
    };

    SECTION ("different types - opt == opt")
    {
        Optional<std::string> opt_string;
        Optional<StringLiteral> opt_literal;

        // none == none
        CHECK(opt_string == opt_literal);
        CHECK_FALSE(opt_string != opt_literal);
        CHECK(opt_literal == opt_string);
        CHECK_FALSE(opt_literal != opt_string);

        // some("") != none
        opt_string = "";
        CHECK_FALSE(opt_string == opt_literal);
        CHECK(opt_string != opt_literal);
        CHECK_FALSE(opt_literal == opt_string);
        CHECK(opt_literal != opt_string);

        // some("") == some("")
        opt_literal = "";
        CHECK(opt_string == opt_literal);
        CHECK_FALSE(opt_string != opt_literal);
        CHECK(opt_literal == opt_string);
        CHECK_FALSE(opt_literal != opt_string);

        // some("hi") != some("")
        opt_string = "hi";
        CHECK_FALSE(opt_string == opt_literal);
        CHECK(opt_string != opt_literal);
        CHECK_FALSE(opt_literal == opt_string);
        CHECK(opt_literal != opt_string);
    };

    SECTION ("different types - opt == raw")
    {
        Optional<std::string> opt_string;
        StringLiteral literal = "";

        // none != ""
        CHECK_FALSE(opt_string == literal);
        CHECK(opt_string != literal);
        CHECK_FALSE(literal == opt_string);
        CHECK(literal != opt_string);

        // some("") == ""
        opt_string = "";
        CHECK(opt_string == literal);
        CHECK_FALSE(opt_string != literal);
        CHECK(literal == opt_string);
        CHECK_FALSE(literal != opt_string);

        // some("hi") != ""
        opt_string = "hi";
        CHECK_FALSE(opt_string == literal);
        CHECK(opt_string != literal);
        CHECK_FALSE(literal == opt_string);
        CHECK(literal != opt_string);
    };
}
