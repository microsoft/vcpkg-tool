#include <catch2/catch.hpp>

#include <vcpkg/base/expected.h>

#include <cstddef>

using namespace vcpkg;

namespace
{
    template<int kind>
    struct ConstructRoot
    {
        std::size_t alive = 0;
        std::size_t copies = 0;
        std::size_t copy_assigns = 0;
        std::size_t moves = 0;
        std::size_t move_assigns = 0;

        void check_no_ops()
        {
            CHECK(copies == 0);
            CHECK(copy_assigns == 0);
            CHECK(moves == 0);
            CHECK(move_assigns == 0);
        }

        void check_nothing()
        {
            CHECK(alive == 0);
            check_no_ops();
        }
    };

    template<int kind>
    struct ConstructTracker
    {
        ConstructRoot<kind>* cr;
        bool moved_from;

        ConstructTracker() = delete;
        ConstructTracker(ConstructRoot<kind>& cr_) : cr(&cr_), moved_from(false) { ++cr->alive; }
        ConstructTracker(const ConstructTracker& other) : cr(other.cr), moved_from(other.moved_from)
        {
            ++cr->alive;
            ++cr->copies;
        }
        ConstructTracker(ConstructTracker&& other) noexcept : cr(other.cr), moved_from(other.moved_from)
        {
            other.moved_from = true;
            ++cr->alive;
            ++cr->moves;
        }
        ConstructTracker& operator=(const ConstructTracker& other)
        {
            if (this != &other)
            {
                --cr->alive;
                cr = other.cr;
                ++cr->alive;
            }

            ++cr->copy_assigns;
            return *this;
        }
        ConstructTracker& operator=(ConstructTracker&& other) noexcept
        {
            if (this != &other)
            {
                other.moved_from = true;
                --cr->alive;
                cr = other.cr;
                ++cr->alive;
            }

            ++cr->move_assigns;
            return *this;
        }
        std::string to_string() const { return "a construct tracker"; }
        ~ConstructTracker() { --cr->alive; }
    };
}

VCPKG_FORMAT_WITH_TO_STRING(ConstructTracker<0>);
VCPKG_FORMAT_WITH_TO_STRING(ConstructTracker<1>);

TEST_CASE ("construct and destroy matching type", "[expected]")
{
    ConstructRoot<0> value;
    ConstructRoot<0> error;

    {
        ExpectedT<ConstructTracker<0>, ConstructTracker<0>> uut{value, expected_left_tag};
        CHECK(value.alive == 1);
        value.check_no_ops();
        error.check_nothing();
    }

    value.check_nothing();
    error.check_nothing();

    {
        ExpectedT<ConstructTracker<0>, ConstructTracker<0>> uut{error, expected_right_tag};
        value.check_nothing();
        CHECK(error.alive == 1);
        error.check_no_ops();
    }

    value.check_nothing();
    error.check_nothing();
}

TEST_CASE ("construct and destroy different type", "[expected]")
{
    ConstructRoot<0> value;
    ConstructRoot<1> error;

    {
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> uut{value};
        CHECK(value.alive == 1);
        value.check_no_ops();
        error.check_nothing();
    }

    value.check_nothing();
    error.check_nothing();

    {
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> uut{error};
        value.check_nothing();
        CHECK(error.alive == 1);
        error.check_no_ops();
    }

    value.check_nothing();
    error.check_nothing();

    {
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> uut{value, expected_left_tag};
        CHECK(value.alive == 1);
        value.check_no_ops();
        error.check_nothing();
    }

    value.check_nothing();
    error.check_nothing();

    {
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> uut{error, expected_right_tag};
        value.check_nothing();
        CHECK(error.alive == 1);
        error.check_no_ops();
    }

    value.check_nothing();
    error.check_nothing();
}

TEST_CASE ("copy and move construction value", "[expected]")
{
    ConstructRoot<0> value;
    ConstructRoot<1> error;
    ExpectedT<ConstructTracker<0>, ConstructTracker<1>> uut{value};
    CHECK(value.alive == 1);
    value.check_no_ops();
    error.check_nothing();
    auto cp = uut;
    CHECK(value.alive == 2);
    CHECK(value.copies == 1);
    CHECK(value.copy_assigns == 0);
    CHECK(value.moves == 0);
    CHECK(value.move_assigns == 0);
    error.check_nothing();
    auto moved = std::move(uut);
    CHECK(value.alive == 3);
    CHECK(value.copies == 1);
    CHECK(value.copy_assigns == 0);
    CHECK(value.moves == 1);
    CHECK(value.move_assigns == 0);
    error.check_nothing();
}

TEST_CASE ("copy and move construction error", "[expected]")
{
    ConstructRoot<0> value;
    ConstructRoot<1> error;
    ExpectedT<ConstructTracker<0>, ConstructTracker<1>> uut{error};
    CHECK(error.alive == 1);
    error.check_no_ops();
    value.check_nothing();
    auto cp = uut;
    CHECK(error.alive == 2);
    CHECK(error.copies == 1);
    CHECK(error.copy_assigns == 0);
    CHECK(error.moves == 0);
    CHECK(error.move_assigns == 0);
    value.check_nothing();
    auto moved = std::move(uut);
    CHECK(error.alive == 3);
    CHECK(error.copies == 1);
    CHECK(error.copy_assigns == 0);
    CHECK(error.moves == 1);
    CHECK(error.move_assigns == 0);
    value.check_nothing();
}

TEST_CASE ("move assignment value value", "[expected]")
{
    ConstructRoot<0> value;
    ConstructRoot<1> error;

    {
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> originally_value{value};
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> originally_value2{value};
        originally_value = std::move(originally_value2);
        CHECK(!originally_value.value_or_exit(VCPKG_LINE_INFO).moved_from);
        CHECK(originally_value2.value_or_exit(VCPKG_LINE_INFO).moved_from);
        CHECK(value.alive == 2);
        CHECK(value.copies == 0);
        CHECK(value.copy_assigns == 0);
        CHECK(value.moves == 0);
        CHECK(value.move_assigns == 1);
        error.check_nothing();
    }

    CHECK(value.alive == 0);
    CHECK(value.copies == 0);
    CHECK(value.copy_assigns == 0);
    CHECK(value.moves == 0);
    CHECK(value.move_assigns == 1);
    error.check_nothing();
}

TEST_CASE ("move assignment value error", "[expected]")
{
    ConstructRoot<0> value;
    ConstructRoot<1> error;

    {
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> originally_value{value};
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> originally_error{error};
        originally_value = std::move(originally_error);
        CHECK(!originally_value.error().moved_from);
        CHECK(originally_error.error().moved_from);
        value.check_nothing();
        CHECK(error.alive == 2);
        CHECK(error.copies == 0);
        CHECK(error.copy_assigns == 0);
        CHECK(error.moves == 1);
        CHECK(error.move_assigns == 0);
    }

    value.check_nothing();
    CHECK(error.alive == 0);
    CHECK(error.copies == 0);
    CHECK(error.copy_assigns == 0);
    CHECK(error.moves == 1);
    CHECK(error.move_assigns == 0);
}

TEST_CASE ("move assignment error value", "[expected]")
{
    ConstructRoot<0> value;
    ConstructRoot<1> error;

    {
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> originally_value{value};
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> originally_error{error};
        originally_error = std::move(originally_value);
        CHECK(originally_value.value_or_exit(VCPKG_LINE_INFO).moved_from);
        CHECK(!originally_error.value_or_exit(VCPKG_LINE_INFO).moved_from);
        error.check_nothing();
        CHECK(value.alive == 2);
        CHECK(value.copies == 0);
        CHECK(value.copy_assigns == 0);
        CHECK(value.moves == 1);
        CHECK(value.move_assigns == 0);
    }

    error.check_nothing();
    CHECK(value.alive == 0);
    CHECK(value.copies == 0);
    CHECK(value.copy_assigns == 0);
    CHECK(value.moves == 1);
    CHECK(value.move_assigns == 0);
}

TEST_CASE ("move assignment error error", "[expected]")
{
    ConstructRoot<0> value;
    ConstructRoot<1> error;

    {
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> originally_error{error};
        ExpectedT<ConstructTracker<0>, ConstructTracker<1>> originally_error2{error};
        originally_error = std::move(originally_error2);
        CHECK(!originally_error.error().moved_from);
        CHECK(originally_error2.error().moved_from);
        CHECK(error.alive == 2);
        CHECK(error.copies == 0);
        CHECK(error.copy_assigns == 0);
        CHECK(error.moves == 0);
        CHECK(error.move_assigns == 1);
        value.check_nothing();
    }

    CHECK(error.alive == 0);
    CHECK(error.copies == 0);
    CHECK(error.copy_assigns == 0);
    CHECK(error.moves == 0);
    CHECK(error.move_assigns == 1);
    value.check_nothing();
}

TEST_CASE ("map", "[expected]")
{
    ConstructRoot<0> value;
    ConstructRoot<1> error;
    using TestType = ExpectedT<ConstructTracker<0>, ConstructTracker<1>>;
    {
        TestType originally_value(value);
        auto result = originally_value.map(
            [&value](auto&& mv) -> std::enable_if_t<std::is_same_v<decltype(mv), const ConstructTracker<0>&>, int> {
                CHECK(!mv.moved_from);
                CHECK(mv.cr == &value);
                return 42;
            });
        static_assert(std::is_same_v<decltype(result), ExpectedT<int, ConstructTracker<1>>>, "Bad map(const&) type");
        CHECK(result.value_or_exit(VCPKG_LINE_INFO) == 42);
    }

    value.check_nothing();
    error.check_nothing();

    {
        TestType originally_value(value);
        auto result =
            std::move(originally_value)
                .map([&value](auto&& mv) -> std::enable_if_t<std::is_same_v<decltype(mv), ConstructTracker<0>&&>, int> {
                    CHECK(!mv.moved_from);
                    CHECK(mv.cr == &value);
                    return 42;
                });
        static_assert(std::is_same_v<decltype(result), ExpectedT<int, ConstructTracker<1>>>, "Bad map(&&) type");
        CHECK(result.value_or_exit(VCPKG_LINE_INFO) == 42);
    }

    value.check_nothing();
    error.check_nothing();

    {
        TestType originally_error(error);
        auto result = originally_error.map(
            [](auto&& mv) -> std::enable_if_t<std::is_same_v<decltype(mv), const ConstructTracker<0>&>, int> {
                FAIL();
                return 42;
            });
        CHECK(result.error().cr == &error);
    }

    value.check_nothing();
    CHECK(error.copies == 1);
    error.copies = 0;
    error.check_nothing();

    {
        TestType originally_error(error);
        auto result =
            std::move(originally_error)
                .map([](auto&& mv) -> std::enable_if_t<std::is_same_v<decltype(mv), ConstructTracker<0>&&>, int> {
                    FAIL();
                    return 42;
                });
        CHECK(result.error().cr == &error);
    }

    value.check_nothing();
    CHECK(error.moves == 1);
    error.moves = 0;
    error.check_nothing();
}

TEST_CASE ("value_or", "[expected]")
{
    std::string value = "hello";
    std::string fill_in_value = "world";
    int error;

    SECTION ("with_value")
    {
        ExpectedT<std::string, int> with_value(value);
        auto result = with_value.value_or(fill_in_value);
        CHECK(result == value);
        CHECK(with_value.has_value());
    }

    SECTION ("with_error")
    {
        ExpectedT<std::string, int> with_error(error);
        auto result = with_error.value_or(fill_in_value);
        CHECK(result == fill_in_value);
        CHECK(!with_error.has_value());
    }

    SECTION ("fill pass args")
    {
        struct Value
        {
            int code;
            std::string message;
            Value() = default;
            Value(int c, const std::string& s) : code(c), message(s) { }
        };

        ExpectedT<Value, int> with_fill_in_value(error);
        auto result = with_fill_in_value.value_or(1, "hello world");
        CHECK(result.code == 1);
        CHECK(result.message == "hello world");
        CHECK(!with_fill_in_value.has_value());
    }
}
