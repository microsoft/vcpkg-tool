#include <vcpkg-test/util.h>

#include <vcpkg/cmake.h>

using namespace vcpkg;

TEST_CASE ("replace CMake variable", "[cmake]")
{
    static constexpr StringLiteral str{"lorem ip${VERSION}"};
    {
        auto res = replace_cmake_var(str, "VERSION", "sum");
        REQUIRE(res == "lorem ipsum");
    }
    {
        auto res = replace_cmake_var(str, "VERSiON", "sum");
        REQUIRE(res == "lorem ip${VERSION}");
    }
}

TEST_CASE ("find cmake invocation", "[cmake]")
{
    {
        auto res = find_cmake_invocation("lorem_ipsum()", "lorem_ipsum");
        REQUIRE(res.empty());
    }
    {
        auto res = find_cmake_invocation("lorem_ipsum()", "lorem_ipsu");
        REQUIRE(res.empty());
    }
    {
        auto res = find_cmake_invocation("lorem_ipsum(", "lorem_ipsum");
        REQUIRE(res.empty());
    }
    {
        auto res = find_cmake_invocation("lorem_ipum()", "lorem_ipsum");
        REQUIRE(res.empty());
    }
    {
        auto res = find_cmake_invocation("lorem_ipsum( )", "lorem_ipsum");
        REQUIRE(res == " ");
    }
}

TEST_CASE ("extract cmake invocation argument", "[cmake]")
{
    {
        auto res = extract_cmake_invocation_argument("loremipsum", "lorem");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_cmake_invocation_argument("lorem", "lorem");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_cmake_invocation_argument("lorem \"", "lorem");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_cmake_invocation_argument("lorem   ", "lorem");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_cmake_invocation_argument("lorem ipsum", "lorem");
        REQUIRE(res == "ipsum");
    }
    {
        auto res = extract_cmake_invocation_argument("lorem \"ipsum", "lorem");
        REQUIRE(res == "ipsum");
    }
    {
        auto res = extract_cmake_invocation_argument("lorem \"ipsum\"", "lorem");
        REQUIRE(res == "ipsum");
    }
}
