#include <vcpkg-test/util.h>

#include <vcpkg/base/stringview.h>

using namespace vcpkg;

template<std::size_t N>
static StringView sv(const char (&cstr)[N])
{
    return cstr;
}

TEST_CASE ("string view operator==", "[stringview]")
{
    // these are due to a bug in operator==
    // see commit 782723959399a1a0725ac49
    REQUIRE(sv("hey") != sv("heys"));
    REQUIRE(sv("heys") != sv("hey"));
    REQUIRE(sv("hey") == sv("hey"));
    REQUIRE(sv("hey") != sv("hex"));
}

TEST_CASE ("zstring_view substr", "[stringview]")
{
    static constexpr StringLiteral example = "text";
    REQUIRE(example.substr(2) == "xt");
    REQUIRE(example.substr(3) == "t");
    REQUIRE(example.substr(4) == "");
    REQUIRE(example.substr(5) == "");
}
