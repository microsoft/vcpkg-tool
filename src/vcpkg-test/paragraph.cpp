#include <catch2/catch.hpp>

#include <vcpkg/base/strings.h>

#include <vcpkg/paragraphs.h>

#include <vcpkg-test/util.h>

namespace Strings = vcpkg::Strings;
using vcpkg::Parse::Paragraph;

namespace
{
    auto test_parse_control_file(const std::vector<std::unordered_map<std::string, std::string>>& v)
    {
        std::vector<Paragraph> pghs;
        for (auto&& p : v)
        {
            pghs.emplace_back();
            for (auto&& kv : p)
                pghs.back().emplace(kv.first, std::make_pair(kv.second, vcpkg::Parse::TextRowCol{}));
        }
        return vcpkg::SourceControlFile::parse_control_file("", std::move(pghs));
    }

    auto test_make_binary_paragraph(const std::unordered_map<std::string, std::string>& v)
    {
        Paragraph pgh;
        for (auto&& kv : v)
            pgh.emplace(kv.first, std::make_pair(kv.second, vcpkg::Parse::TextRowCol{}));

        return vcpkg::BinaryParagraph(std::move(pgh));
    }

}

TEST_CASE ("SourceParagraph construct minimum", "[paragraph]")
{
    auto m_pgh = test_parse_control_file({{
        {"Source", "zlib"},
        {"Version", "1.2.8"},
    }});

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->name == "zlib");
    REQUIRE(pgh.core_paragraph->version == "1.2.8");
    REQUIRE(pgh.core_paragraph->maintainers.empty());
    REQUIRE(pgh.core_paragraph->description.empty());
    REQUIRE(pgh.core_paragraph->dependencies.size() == 0);
}

TEST_CASE ("SourceParagraph construct invalid", "[paragraph]")
{
    auto m_pgh = test_parse_control_file({{
        {"Source", "zlib"},
        {"Version", "1.2.8"},
        {"Build-Depends", "1.2.8"},
    }});

    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error()->has_error());

    m_pgh = test_parse_control_file({{
        {"Source", "zlib"},
        {"Version", "1.2.8"},
        {"Default-Features", "1.2.8"},
    }});

    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error()->has_error());

    m_pgh = test_parse_control_file({
        {
            {"Source", "zlib"},
            {"Version", "1.2.8"},
        },
        {
            {"Feature", "a"},
            {"Build-Depends", "1.2.8"},
        },
    });

    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error()->has_error());

    // invalid field`s name
    m_pgh = test_parse_control_file({{
        {"Surce", "zlib"},
        {"Vursion", "1.2.8"},
    }});

    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error()->has_error());
}

TEST_CASE ("SourceParagraph construct maximum", "[paragraph]")
{
    auto m_pgh = test_parse_control_file({{
        {"Source", "s"},
        {"Version", "v"},
        {"Maintainer", "m"},
        {"Description", "d"},
        {"Build-Depends", "bd"},
        {"Default-Features", "df"},
    }});
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->name == "s");
    REQUIRE(pgh.core_paragraph->version == "v");
    REQUIRE(pgh.core_paragraph->maintainers.size() == 1);
    REQUIRE(pgh.core_paragraph->maintainers[0] == "m");
    REQUIRE(pgh.core_paragraph->description.size() == 1);
    REQUIRE(pgh.core_paragraph->description[0] == "d");
    REQUIRE(pgh.core_paragraph->dependencies.size() == 1);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "bd");
    REQUIRE(pgh.core_paragraph->default_features.size() == 1);
    REQUIRE(pgh.core_paragraph->default_features[0] == "df");
}

TEST_CASE ("SourceParagraph construct feature", "[paragraph]")
{
    auto m_pgh = test_parse_control_file({
        {
            {"Source", "s"},
            {"Version", "v"},
        },
        {{"Feature", "f"}, {"Description", "d2"}, {"Build-Depends", "bd2"}},
    });
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.feature_paragraphs.size() == 1);
    REQUIRE(pgh.feature_paragraphs[0]->name == "f");
    REQUIRE(pgh.feature_paragraphs[0]->description == std::vector<std::string>{"d2"});
    REQUIRE(pgh.feature_paragraphs[0]->dependencies.size() == 1);
}

TEST_CASE ("SourceParagraph two dependencies", "[paragraph]")
{
    auto m_pgh = test_parse_control_file({{
        {"Source", "zlib"},
        {"Version", "1.2.8"},
        {"Build-Depends", "z, openssl"},
    }});
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->dependencies.size() == 2);
    // should be ordered
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "openssl");
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "z");
}

TEST_CASE ("SourceParagraph three dependencies", "[paragraph]")
{
    auto m_pgh = test_parse_control_file({{
        {"Source", "zlib"},
        {"Version", "1.2.8"},
        {"Build-Depends", "z, openssl, xyz"},
    }});
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->dependencies.size() == 3);
    // should be ordered
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "openssl");
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "xyz");
    REQUIRE(pgh.core_paragraph->dependencies[2].name == "z");
}

TEST_CASE ("SourceParagraph construct qualified dependencies", "[paragraph]")
{
    auto m_pgh = test_parse_control_file({{
        {"Source", "zlib"},
        {"Version", "1.2.8"},
        {"Build-Depends", "liba (windows), libb (uwp)"},
    }});
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->name == "zlib");
    REQUIRE(pgh.core_paragraph->version == "1.2.8");
    REQUIRE(pgh.core_paragraph->maintainers.empty());
    REQUIRE(pgh.core_paragraph->description.empty());
    REQUIRE(pgh.core_paragraph->dependencies.size() == 2);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "liba");
    REQUIRE(pgh.core_paragraph->dependencies[0].platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "libb");
    REQUIRE(pgh.core_paragraph->dependencies[1].platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
}

TEST_CASE ("SourceParagraph default features", "[paragraph]")
{
    auto m_pgh = test_parse_control_file({{
        {"Source", "a"},
        {"Version", "1.0"},
        {"Default-Features", "a1"},
    }});
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->default_features.size() == 1);
    REQUIRE(pgh.core_paragraph->default_features[0] == "a1");
}

TEST_CASE ("BinaryParagraph construct minimum", "[paragraph]")
{
    auto pgh = test_make_binary_paragraph({
        {"Package", "zlib"},
        {"Version", "1.2.8"},
        {"Architecture", "x86-windows"},
        {"Multi-Arch", "same"},
    });

    REQUIRE(pgh.spec.name() == "zlib");
    REQUIRE(pgh.version == "1.2.8");
    REQUIRE(pgh.maintainers.empty());
    REQUIRE(pgh.description.empty());
    REQUIRE(pgh.spec.triplet().canonical_name() == "x86-windows");
    REQUIRE(pgh.dependencies.size() == 0);
}

TEST_CASE ("BinaryParagraph construct maximum", "[paragraph]")
{
    auto pgh = test_make_binary_paragraph({
        {"Package", "s"},
        {"Version", "v"},
        {"Architecture", "x86-windows"},
        {"Multi-Arch", "same"},
        {"Maintainer", "m"},
        {"Description", "d"},
        {"Depends", "bd"},
    });

    REQUIRE(pgh.spec.name() == "s");
    REQUIRE(pgh.version == "v");
    REQUIRE(pgh.maintainers.size() == 1);
    REQUIRE(pgh.maintainers[0] == "m");
    REQUIRE(pgh.description.size() == 1);
    REQUIRE(pgh.description[0] == "d");
    REQUIRE(pgh.dependencies.size() == 1);
    REQUIRE(pgh.dependencies[0].name() == "bd");
}

TEST_CASE ("BinaryParagraph three dependencies", "[paragraph]")
{
    auto pgh = test_make_binary_paragraph({
        {"Package", "zlib"},
        {"Version", "1.2.8"},
        {"Architecture", "x86-windows"},
        {"Multi-Arch", "same"},
        {"Depends", "a, b, c"},
    });

    REQUIRE(pgh.dependencies.size() == 3);
    REQUIRE(pgh.dependencies[0].name() == "a");
    REQUIRE(pgh.dependencies[1].name() == "b");
    REQUIRE(pgh.dependencies[2].name() == "c");
}

TEST_CASE ("BinaryParagraph dependencies with triplets", "[paragraph]")
{
    auto pgh = test_make_binary_paragraph({
        {"Package", "zlib"},
        {"Version", "1.2.8"},
        {"Architecture", "x86-windows"},
        {"Multi-Arch", "same"},
        {"Depends", "a:x64-windows, b, c:arm-uwp"},
    });

    REQUIRE(pgh.dependencies.size() == 3);
    REQUIRE(pgh.dependencies[0].name() == "a");
    REQUIRE(pgh.dependencies[0].triplet() == vcpkg::Test::X64_WINDOWS);
    REQUIRE(pgh.dependencies[1].name() == "b");
    REQUIRE(pgh.dependencies[1].triplet() == vcpkg::Test::X86_WINDOWS);
    REQUIRE(pgh.dependencies[2].name() == "c");
    REQUIRE(pgh.dependencies[2].triplet() == vcpkg::Test::ARM_UWP);
}

TEST_CASE ("BinaryParagraph abi", "[paragraph]")
{
    auto pgh = test_make_binary_paragraph({
        {"Package", "zlib"},
        {"Version", "1.2.8"},
        {"Architecture", "x86-windows"},
        {"Multi-Arch", "same"},
        {"Abi", "abcd123"},
    });

    REQUIRE(pgh.dependencies.size() == 0);
    REQUIRE(pgh.abi == "abcd123");
}

TEST_CASE ("BinaryParagraph default features", "[paragraph]")
{
    auto pgh = test_make_binary_paragraph({
        {"Package", "a"},
        {"Version", "1.0"},
        {"Architecture", "x86-windows"},
        {"Multi-Arch", "same"},
        {"Default-Features", "a1"},
    });

    REQUIRE(pgh.dependencies.size() == 0);
    REQUIRE(pgh.default_features.size() == 1);
    REQUIRE(pgh.default_features[0] == "a1");
}

TEST_CASE ("parse paragraphs empty", "[paragraph]")
{
    const char* str = "";
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(str, "").value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(pghs.empty());
}

TEST_CASE ("parse paragraphs one field", "[paragraph]")
{
    const char* str = "f1: v1";
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(str, "").value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(pghs.size() == 1);
    REQUIRE(pghs[0].size() == 1);
    REQUIRE(pghs[0]["f1"].first == "v1");
}

TEST_CASE ("parse paragraphs one pgh", "[paragraph]")
{
    const char* str = "f1: v1\n"
                      "f2: v2";
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(str, "").value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(pghs.size() == 1);
    REQUIRE(pghs[0].size() == 2);
    REQUIRE(pghs[0]["f1"].first == "v1");
    REQUIRE(pghs[0]["f2"].first == "v2");
}

TEST_CASE ("parse paragraphs two pgh", "[paragraph]")
{
    const char* str = "f1: v1\n"
                      "f2: v2\n"
                      "\n"
                      "f3: v3\n"
                      "f4: v4";
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(str, "").value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(pghs.size() == 2);
    REQUIRE(pghs[0].size() == 2);
    REQUIRE(pghs[0]["f1"].first == "v1");
    REQUIRE(pghs[0]["f2"].first == "v2");
    REQUIRE(pghs[1].size() == 2);
    REQUIRE(pghs[1]["f3"].first == "v3");
    REQUIRE(pghs[1]["f4"].first == "v4");
}

TEST_CASE ("parse paragraphs field names", "[paragraph]")
{
    const char* str = "1:\n"
                      "f:\n"
                      "F:\n"
                      "0:\n"
                      "F-2:\n";
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(str, "").value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(pghs.size() == 1);
    REQUIRE(pghs[0].size() == 5);
}

TEST_CASE ("parse paragraphs multiple blank lines", "[paragraph]")
{
    const char* str = "f1: v1\n"
                      "f2: v2\n"
                      "\n"
                      "\n"
                      "f3: v3\n"
                      "f4: v4";
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(str, "").value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(pghs.size() == 2);
}

TEST_CASE ("parse paragraphs empty fields", "[paragraph]")
{
    const char* str = "f1:\n"
                      "f2: ";
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(str, "").value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(pghs.size() == 1);
    REQUIRE(pghs[0].size() == 2);
    REQUIRE(pghs[0]["f1"].first.empty());
    REQUIRE(pghs[0]["f2"].first.empty());
    REQUIRE(pghs[0].size() == 2);
}

TEST_CASE ("parse paragraphs multiline fields", "[paragraph]")
{
    const char* str = "f1: simple\n"
                      " f1\r\n"
                      "f2:\r\n"
                      " f2\r\n"
                      " continue\r\n";
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(str, "").value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(pghs.size() == 1);
    REQUIRE(pghs[0]["f1"].first == "simple\n f1");
    REQUIRE(pghs[0]["f2"].first == "\n f2\n continue");
}

TEST_CASE ("parse paragraphs crlfs", "[paragraph]")
{
    const char* str = "f1: v1\r\n"
                      "f2: v2\r\n"
                      "\r\n"
                      "f3: v3\r\n"
                      "f4: v4";
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(str, "").value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(pghs.size() == 2);
    REQUIRE(pghs[0].size() == 2);
    REQUIRE(pghs[0]["f1"].first == "v1");
    REQUIRE(pghs[0]["f2"].first == "v2");
    REQUIRE(pghs[1].size() == 2);
    REQUIRE(pghs[1]["f3"].first == "v3");
    REQUIRE(pghs[1]["f4"].first == "v4");
}

TEST_CASE ("parse paragraphs comment", "[paragraph]")
{
    const char* str = "f1: v1\r\n"
                      "#comment\r\n"
                      "f2: v2\r\n"
                      "#comment\r\n"
                      "\r\n"
                      "#comment\r\n"
                      "f3: v3\r\n"
                      "#comment\r\n"
                      "f4: v4";
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(str, "").value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(pghs.size() == 2);
    REQUIRE(pghs[0].size() == 2);
    REQUIRE(pghs[0]["f1"].first == "v1");
    REQUIRE(pghs[0]["f2"].first == "v2");
    REQUIRE(pghs[1].size());
    REQUIRE(pghs[1]["f3"].first == "v3");
    REQUIRE(pghs[1]["f4"].first == "v4");
}

TEST_CASE ("parse comment before single line feed", "[paragraph]")
{
    const char* str = "f1: v1\r\n"
                      "#comment\n";
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(str, "").value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(pghs[0].size() == 1);
    REQUIRE(pghs[0]["f1"].first == "v1");
}

TEST_CASE ("BinaryParagraph serialize min", "[paragraph]")
{
    auto pgh = test_make_binary_paragraph({
        {"Package", "zlib"},
        {"Version", "1.2.8"},
        {"Architecture", "x86-windows"},
        {"Multi-Arch", "same"},
    });
    std::string ss = Strings::serialize(pgh);
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(ss, "").value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(pghs.size() == 1);
    REQUIRE(pghs[0].size() == 5);
    REQUIRE(pghs[0]["Package"].first == "zlib");
    REQUIRE(pghs[0]["Version"].first == "1.2.8");
    REQUIRE(pghs[0]["Architecture"].first == "x86-windows");
    REQUIRE(pghs[0]["Multi-Arch"].first == "same");
    REQUIRE(pghs[0]["Type"].first == "Port");
}

TEST_CASE ("BinaryParagraph serialize max", "[paragraph]")
{
    auto pgh = test_make_binary_paragraph({
        {"Package", "zlib"},
        {"Version", "1.2.8"},
        {"Architecture", "x86-windows"},
        {"Description", "first line\n second line"},
        {"Maintainer", "abc <abc@abc.abc>"},
        {"Depends", "dep"},
        {"Multi-Arch", "same"},
    });
    std::string ss = Strings::serialize(pgh);
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(ss, "").value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(pghs.size() == 1);
    REQUIRE(pghs[0].size() == 8);
    REQUIRE(pghs[0]["Package"].first == "zlib");
    REQUIRE(pghs[0]["Version"].first == "1.2.8");
    REQUIRE(pghs[0]["Architecture"].first == "x86-windows");
    REQUIRE(pghs[0]["Multi-Arch"].first == "same");
    REQUIRE(pghs[0]["Description"].first == "first line\n    second line");
    REQUIRE(pghs[0]["Depends"].first == "dep");
    REQUIRE(pghs[0]["Type"].first == "Port");
}

TEST_CASE ("BinaryParagraph serialize multiple deps", "[paragraph]")
{
    SECTION ("target only")
    {
        auto pgh = test_make_binary_paragraph({
            {"Package", "zlib"},
            {"Version", "1.2.8"},
            {"Architecture", "x86-windows"},
            {"Multi-Arch", "same"},
            {"Depends", "a, b, c"},
        });
        std::string ss = Strings::serialize(pgh);
        auto pghs = vcpkg::Paragraphs::parse_paragraphs(ss, "").value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(pghs.size() == 1);
        REQUIRE(pghs[0]["Depends"].first == "a, b, c");
    }
    SECTION ("host deps")
    {
        auto pgh = test_make_binary_paragraph({
            {"Package", "zlib"},
            {"Version", "1.2.8"},
            {"Architecture", "x86-windows"},
            {"Multi-Arch", "same"},
            {"Depends", "a:x64-windows, b, c:arm-uwp"},
        });
        std::string ss = Strings::serialize(pgh);
        auto pghs = vcpkg::Paragraphs::parse_paragraphs(ss, "").value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(pghs.size() == 1);
        REQUIRE(pghs[0]["Depends"].first == "a:x64-windows, b, c:arm-uwp");
    }
}

TEST_CASE ("BinaryParagraph serialize abi", "[paragraph]")
{
    auto pgh = test_make_binary_paragraph({
        {"Package", "zlib"},
        {"Version", "1.2.8"},
        {"Architecture", "x86-windows"},
        {"Multi-Arch", "same"},
        {"Depends", "a, b, c"},
        {"Abi", "123abc"},
    });
    std::string ss = Strings::serialize(pgh);
    auto pghs = vcpkg::Paragraphs::parse_paragraphs(ss, "").value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(pghs.size() == 1);
    REQUIRE(pghs[0]["Abi"].first == "123abc");
}
