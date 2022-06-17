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

TEST_CASE ("platform-expression-not-alternate", "[platform-expression]")
{
    {
        auto m_expr = parse_expr("not windows");
        REQUIRE(m_expr);
        auto& expr = *m_expr.get();

        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));
    }

    {
        auto m_expr = parse_expr("not windows & not arm & not x86");
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

    {
        auto m_expr = parse_expr("not windows and !arm & not x86");
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

TEST_CASE ("platform-expression-and-alternate", "[platform-expression]")
{
    auto m_expr = parse_expr("!windows and !arm");
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

TEST_CASE ("platform-expression-and-multiple", "[platform-expression]")
{
    {
        auto m_expr = parse_expr("!windows & !arm & !x86");
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

    {
        auto m_expr = parse_expr("!windows and !arm and !x86");
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

TEST_CASE ("platform-expression-or-alternate", "[platform-expression]")
{
    auto m_expr = parse_expr("!windows , arm");
    REQUIRE(m_expr);
    auto& expr = *m_expr.get();

    CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
}

TEST_CASE ("platform-expression-or-multiple", "[platform-expression]")
{
    {
        auto m_expr = parse_expr("!windows | linux | arm");
        REQUIRE(m_expr);
        auto& expr = *m_expr.get();

        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    }

    {
        auto m_expr = parse_expr("!windows , linux , arm");
        REQUIRE(m_expr);
        auto& expr = *m_expr.get();

        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    }
}

TEST_CASE ("platform-expression-mixed-with-parens", "[platform-expression]")
{
    auto m_expr = parse_expr("(x64 | arm64) & (linux | osx | windows)");
    REQUIRE(m_expr);
    auto& expr = *m_expr.get();

    CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    CHECK_FALSE(expr.evaluate({{"VCPKG_TARGET_ARCHITECTURE", ""}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
    CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
}

TEST_CASE ("platform-expression-low-precedence-or", "[platform-expression]")
{
    {
        auto m_expr = parse_expr("(x64 & windows) , (linux & arm)");
        REQUIRE(m_expr);
        auto& expr = *m_expr.get();

        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_TARGET_ARCHITECTURE", ""}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK_FALSE(
            expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    }

    {
        auto m_expr = parse_expr("x64 & windows , linux & arm");
        REQUIRE(m_expr);
        auto& expr = *m_expr.get();

        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_TARGET_ARCHITECTURE", ""}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK_FALSE(
            expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    }
}

TEST_CASE ("mixing &/'and' and , is allowed", "[platform-expression]")
{
    {
        auto m_expr = parse_expr("windows & x86 , linux and x64 , arm64 & osx");
        CHECK(m_expr);
        auto& expr = *m_expr.get();

        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_TARGET_ARCHITECTURE", ""}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
        CHECK_FALSE(
            expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    }
    {
        auto m_expr = parse_expr("windows , !arm and linux & (x86 | x64)");
        CHECK(m_expr);
        auto& expr = *m_expr.get();

        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_TARGET_ARCHITECTURE", ""}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    }
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

TEST_CASE ("platform-expressions without whitespace", "[platform-expression]")
{
    {
        auto m_expr = parse_expr("!windows|linux|arm");
        REQUIRE(m_expr);
        auto& expr = *m_expr.get();

        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    }

    {
        auto m_expr = parse_expr("!windows&!arm&!x86");
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

    {
        auto m_expr = parse_expr("windows,!arm&linux&(x86|x64)");
        REQUIRE(m_expr);
        auto& expr = *m_expr.get();

        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_TARGET_ARCHITECTURE", ""}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    }
}

TEST_CASE ("operator keywords in identifiers", "[platform-expression]")
{
    // Operator keywords ("and","not") require a break to separate them from identifiers
    // In these cases, strings containing an operator keyword parse as an identifier, not as a unary/binary expression
    CHECK(parse_expr("!windowsandandroid"));
    CHECK(parse_expr("notwindows"));
}

TEST_CASE ("operator keywords without whitepace", "[platform-expression]")
{
    // Operator keywords ("and","not") require a break to separate them from identifiers
    // A break could be whitespace or a grouped expression (e.g., '(A&B)').
    {
        auto m_expr = parse_expr("(!windows)and(!arm)and(!x86)");
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

    {
        auto m_expr = parse_expr("windows , (!arm )and( linux)and( (x86 | x64) )");
        CHECK(m_expr);
        auto& expr = *m_expr.get();

        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_TARGET_ARCHITECTURE", ""}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "arm64"}}));
    }

    {
        auto m_expr = parse_expr("not( !windows& not(x64) )");
        REQUIRE(m_expr);
        auto& expr = *m_expr.get();

        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
        CHECK(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}, {"VCPKG_TARGET_ARCHITECTURE", "x64"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
        CHECK_FALSE(expr.evaluate({{"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
    }
}

TEST_CASE ("invalid logic expression, unexpected character", "[platform-expression]")
{
    CHECK_FALSE(parse_expr("windows arm"));
}

TEST_CASE ("invalid logic expression, use '|' instead of 'or'", "[platform-expression]")
{
    CHECK_FALSE(parse_expr("windows or arm"));
}

TEST_CASE ("unexpected character or identifier in logic expression", "[platform-expression]")
{
    CHECK_FALSE(parse_expr("windows aND arm"));
    CHECK_FALSE(parse_expr("windows a&d arm"));
    CHECK_FALSE(parse_expr("windows oR arm"));
    CHECK_FALSE(parse_expr("windows o|r arm"));
}

TEST_CASE ("unexpected identifier in logic expression", "[platform-expression]")
{
    CHECK_FALSE(parse_expr("windows amd arm"));
    CHECK_FALSE(parse_expr("windows andsynonym arm"));
}

TEST_CASE ("missing closing )", "[platform-expression]")
{
    CHECK_FALSE(parse_expr("(windows & arm | linux"));
    CHECK_FALSE(parse_expr("( (windows & arm) | (osx & arm64) | linux"));
}

TEST_CASE ("missing or invalid identifier", "[platform-expression]")
{
    CHECK_FALSE(parse_expr("!"));
    CHECK_FALSE(parse_expr("w!ndows"));
}

TEST_CASE ("mixing & and | is not allowed", "[platform-expression]")
{
    CHECK_FALSE(parse_expr("windows & arm | linux"));
    CHECK_FALSE(parse_expr("windows | !arm & linux"));
}

TEST_CASE ("invalid expression, no binary operator", "[platform-expression]")
{
    CHECK_FALSE(parse_expr("windows linux"));
    CHECK_FALSE(parse_expr("windows x64"));
    CHECK_FALSE(parse_expr("!windows x86"));
}

TEST_CASE ("invalid expression, missing binary operand", "[platform-expression]")
{
    CHECK_FALSE(parse_expr("windows & "));
    CHECK_FALSE(parse_expr(" | arm"));
    CHECK_FALSE(parse_expr("windows & !arm & "));
}

TEST_CASE ("invalid identifier", "[platform-expression]")
{
    CHECK_FALSE(parse_expr("windows & x^$"));
}

TEST_CASE ("invalid alternate expressions", "[platform-expression]")
{
    CHECK_FALSE(parse_expr("windows an%d arm"));
    CHECK_FALSE(parse_expr("windows aNd arm"));
    CHECK_FALSE(parse_expr("windows andMORE arm"));
    CHECK_FALSE(parse_expr("windows and+ arm"));
    CHECK_FALSE(parse_expr("windows and& arm"));
    CHECK_FALSE(parse_expr("notANY windows"));
    CHECK_FALSE(parse_expr("not! windows"));
    CHECK_FALSE(parse_expr("notx64 windows"));
}
