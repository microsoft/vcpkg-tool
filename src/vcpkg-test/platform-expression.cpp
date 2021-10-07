#include <catch2/catch.hpp>

#include <vcpkg/platform-expression.h>

using vcpkg::StringView;
using namespace vcpkg::PlatformExpression;

static vcpkg::ExpectedS<Expr> parse_expr(StringView s)
{
    return parse_platform_expression(s, MultipleBinaryOperators::Deny);
}

TEST_CASE ("platform-expression-identifier-os", "[platform-expression]")
{
    auto m_windows = parse_expr("windows");
    REQUIRE(m_windows);
    auto m_osx = parse_expr("osx");
    REQUIRE(m_osx);
    auto m_linux = parse_expr("linux");
    REQUIRE(m_linux);

    auto& windows = *m_windows.get();
    auto& osx = *m_osx.get();
    auto& linux = *m_linux.get();

    CHECK(windows.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    CHECK(windows.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
    CHECK_FALSE(windows.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    CHECK_FALSE(windows.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));

    CHECK_FALSE(osx.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    CHECK_FALSE(osx.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
    CHECK_FALSE(osx.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    CHECK(osx.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));

    CHECK_FALSE(linux.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    CHECK_FALSE(linux.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
    CHECK(linux.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    CHECK_FALSE(linux.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));
}

TEST_CASE ("platform-expression-identifier-arch", "[platform-expression]")
{
    auto m_arm = parse_expr("arm");
    REQUIRE(m_arm);
    auto m_arm32 = parse_expr("arm32");
    REQUIRE(m_arm32);
    auto m_arm64 = parse_expr("arm64");
    REQUIRE(m_arm64);
    auto m_x86 = parse_expr("x86");
    REQUIRE(m_x86);
    auto m_x64 = parse_expr("x64");
    REQUIRE(m_x64);
    auto m_wasm32 = parse_expr("wasm32");
    REQUIRE(m_wasm32);

    auto& arm = *m_arm.get();
    auto& arm32 = *m_arm32.get();
    auto& arm64 = *m_arm64.get();
    auto& x86 = *m_x86.get();
    auto& x64 = *m_x64.get();
    auto& wasm32 = *m_wasm32.get();

    CHECK(arm.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    CHECK(arm.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    CHECK_FALSE(arm.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
    CHECK_FALSE(arm.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
    CHECK_FALSE(arm.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "wasm32"}}));

    CHECK(arm32.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    CHECK_FALSE(arm32.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    CHECK_FALSE(arm32.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
    CHECK_FALSE(arm32.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
    CHECK_FALSE(arm32.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "wasm32"}}));

    CHECK_FALSE(arm64.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    CHECK(arm64.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    CHECK_FALSE(arm64.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
    CHECK_FALSE(arm64.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
    CHECK_FALSE(arm64.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "wasm32"}}));

    CHECK_FALSE(x86.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    CHECK_FALSE(x86.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    CHECK(x86.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
    CHECK_FALSE(x86.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
    CHECK_FALSE(x86.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "wasm32"}}));

    CHECK_FALSE(x64.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    CHECK_FALSE(x64.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    CHECK_FALSE(x64.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
    CHECK(x64.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
    CHECK_FALSE(x64.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "wasm32"}}));

    CHECK_FALSE(wasm32.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    CHECK_FALSE(wasm32.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    CHECK_FALSE(wasm32.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
    CHECK_FALSE(wasm32.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
    CHECK(wasm32.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "wasm32"}}));
}

TEST_CASE ("platform-expression-identifier-misc", "[platform-expression]")
{
    auto m_native = parse_expr("native");
    REQUIRE(m_native);
    auto m_staticlink = parse_expr("static");
    REQUIRE(m_staticlink);
    auto m_staticcrt = parse_expr("staticcrt");
    REQUIRE(m_staticcrt);

    auto& native = *m_native.get();
    auto& staticlink = *m_staticlink.get();
    auto& staticcrt = *m_staticcrt.get();

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
