#include <catch2/catch.hpp>

#include <vcpkg/platform-expression.h>

using vcpkg::StringView;
using namespace vcpkg::PlatformExpression;

static vcpkg::ExpectedS<Expr> parse_expr(StringView s)
{
    return parse_platform_expression(s, MultipleBinaryOperators::Deny);
}

TEST_CASE ("platform-expression-identifier", "[platform-expression]")
{
    auto m_windows = parse_expr("windows");
    REQUIRE(m_windows);
    auto m_native = parse_expr("native");
    REQUIRE(m_native);
    auto m_staticlink = parse_expr("static");
    REQUIRE(m_staticlink);
    auto m_staticcrt = parse_expr("staticcrt");
    REQUIRE(m_staticcrt);

    auto& windows = *m_windows.get();
    auto& native = *m_native.get();
    auto& staticlink = *m_staticlink.get();
    auto& staticcrt = *m_staticcrt.get();

    CHECK(windows.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    CHECK(windows.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
    CHECK_FALSE(windows.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    CHECK_FALSE(windows.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));

    CHECK(native.evaluate({{"Z_VCPKG_IS_NATIVE", "1"}}));
    CHECK_FALSE(native.evaluate({{"Z_VCPKG_IS_NATIVE", "0"}}));

    CHECK(staticlink.evaluate({{"VCPKG_LIBRARY_LINKAGE", "static"}, {"VCPKG_CRT_LINKAGE", "static"}}));
    CHECK(staticlink.evaluate({{"VCPKG_LIBRARY_LINKAGE", "static"}, {"VCPKG_CRT_LINKAGE", "dynamic"}}));
    CHECK_FALSE(staticlink.evaluate({{"VCPKG_LIBRARY_LINKAGE", "dynamic"}, {"VCPKG_CRT_LINKAGE", "static"}}));
    CHECK_FALSE(staticlink.evaluate({{"VCPKG_LIBRARY_LINKAGE", "dynnamic"}, {"VCPKG_CRT_LINKAGE", "dynamic"}}));

    CHECK(staticcrt.evaluate({{"VCPKG_CRT_LINKAGE", "static"}, {"VCPKG_LIBRARY_LINKAGE", "static"}}));
    CHECK(staticcrt.evaluate({{"VCPKG_CRT_LINKAGE", "static"}, {"VCPKG_LIBRARY_LINKAGE", "dynamic"}}));
    CHECK_FALSE(staticcrt.evaluate({{"VCPKG_CRT_LINKAGE", "dynamic"}, {"VCPKG_LIBRARY_LINKAGE", "static"}}));
    CHECK_FALSE(staticcrt.evaluate({{"VCPKG_CRT_LINKAGE", "dynamic"}, {"VCPKG_LIBRARY_LINKAGE", "dynamic"}}));
}

TEST_CASE ("platform-expression-not", "[platform-expression]")
{
    auto m_expr = parse_expr("!windows");
    REQUIRE(m_expr);
    auto& expr = *m_expr.get();

    CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));
}

TEST_CASE ("platform-expression-and", "[platform-expression]")
{
    auto m_expr = parse_expr("!windows & !arm");
    REQUIRE(m_expr);
    auto& expr = *m_expr.get();

    CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    CHECK_FALSE(expr.evaluate({
        {"VCPKG_CMAKE_SYSTEM_NAME", "Linux"},
        {"VCPKG_TARGET_ARCHITECTURE", "arm"},
    }));
}

TEST_CASE ("platform-expression-or", "[platform-expression]")
{
    auto m_expr = parse_expr("!windows | arm");
    REQUIRE(m_expr);
    auto& expr = *m_expr.get();

    CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
}

TEST_CASE ("weird platform-expressions whitespace", "[platform-expression]")
{
    auto m_expr = parse_expr(" ! \t  windows \n| arm \r");
    REQUIRE(m_expr);
    auto& expr = *m_expr.get();

    CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
}

TEST_CASE ("no mixing &, | in platform expressions", "[platform-expression]")
{
    auto m_expr = parse_expr("windows & arm | linux");
    CHECK_FALSE(m_expr);
    m_expr = parse_expr("windows | !arm & linux");
    CHECK_FALSE(m_expr);
}
