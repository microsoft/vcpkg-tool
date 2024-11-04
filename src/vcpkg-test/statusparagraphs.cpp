#include <vcpkg-test/util.h>

#include <vcpkg/base/util.h>

#include <vcpkg/paragraphs.h>
#include <vcpkg/statusparagraphs.h>

using namespace vcpkg;
using namespace vcpkg::Paragraphs;
using namespace vcpkg::Test;

static constexpr StringLiteral test_origin = "test";
static constexpr TextRowCol test_textrowcol = {42, 34};

TEST_CASE ("parse status lines", "[statusparagraphs]")
{
    REQUIRE(parse_status_line("install ok installed", test_origin, test_textrowcol).value_or_exit(VCPKG_LINE_INFO) ==
            StatusLine{Want::INSTALL, InstallState::INSTALLED});
    REQUIRE(parse_status_line("hold ok installed", test_origin, test_textrowcol).value_or_exit(VCPKG_LINE_INFO) ==
            StatusLine{Want::HOLD, InstallState::INSTALLED});
    REQUIRE(parse_status_line("deinstall ok installed", test_origin, test_textrowcol).value_or_exit(VCPKG_LINE_INFO) ==
            StatusLine{Want::DEINSTALL, InstallState::INSTALLED});
    REQUIRE(parse_status_line("purge ok installed", test_origin, test_textrowcol).value_or_exit(VCPKG_LINE_INFO) ==
            StatusLine{Want::PURGE, InstallState::INSTALLED});

    REQUIRE(
        parse_status_line("install ok not-installed", test_origin, test_textrowcol).value_or_exit(VCPKG_LINE_INFO) ==
        StatusLine{Want::INSTALL, InstallState::NOT_INSTALLED});
    REQUIRE(
        parse_status_line("install ok half-installed", test_origin, test_textrowcol).value_or_exit(VCPKG_LINE_INFO) ==
        StatusLine{Want::INSTALL, InstallState::HALF_INSTALLED});

    REQUIRE(parse_status_line("meow ok installed", test_origin, test_textrowcol).error() ==
            LocalizedString::from_raw("test:42:34: error: expected one of 'install', 'hold', 'deinstall', or 'purge' "
                                      "here\n  on expression: meow ok installed\n                 ^"));
    REQUIRE(parse_status_line("install ko half-installed", test_origin, test_textrowcol).error() ==
            LocalizedString::from_raw("test:42:41: error: expected ' ok ' here\n  on expression: install ko "
                                      "half-installed\n                        ^"));
    REQUIRE(parse_status_line("install ok meow", test_origin, test_textrowcol).error() ==
            LocalizedString::from_raw("test:42:45: error: expected one of 'not-installed', 'half-installed', or "
                                      "'installed'\n  on expression: install ok meow\n                            ^"));
}

TEST_CASE ("find installed", "[statusparagraphs]")
{
    auto pghs = parse_paragraphs(R"(
Package: ffmpeg
Version: 3.3.3
Architecture: x64-windows
Multi-Arch: same
Description:
Status: install ok installed
)",
                                 "test-origin");

    REQUIRE(pghs);

    StatusParagraphs status_db(Util::fmap(
        *pghs.get(), [](Paragraph& rpgh) { return std::make_unique<StatusParagraph>(test_origin, std::move(rpgh)); }));

    auto it = status_db.find_installed({"ffmpeg", Test::X64_WINDOWS});
    REQUIRE(it != status_db.end());
}

TEST_CASE ("find not installed", "[statusparagraphs]")
{
    auto pghs = parse_paragraphs(R"(
Package: ffmpeg
Version: 3.3.3
Architecture: x64-windows
Multi-Arch: same
Description:
Status: purge ok not-installed
)",
                                 "test-origin");

    REQUIRE(pghs);

    StatusParagraphs status_db(Util::fmap(
        *pghs.get(), [](Paragraph& rpgh) { return std::make_unique<StatusParagraph>(test_origin, std::move(rpgh)); }));

    auto it = status_db.find_installed({"ffmpeg", Test::X64_WINDOWS});
    REQUIRE(it == status_db.end());
}

TEST_CASE ("find with feature packages", "[statusparagraphs]")
{
    auto pghs = parse_paragraphs(R"(
Package: ffmpeg
Version: 3.3.3
Architecture: x64-windows
Multi-Arch: same
Description:
Status: install ok installed

Package: ffmpeg
Feature: openssl
Depends: openssl
Architecture: x64-windows
Multi-Arch: same
Description:
Status: purge ok not-installed
)",
                                 "test-origin");

    REQUIRE(pghs);

    StatusParagraphs status_db(Util::fmap(
        *pghs.get(), [](Paragraph& rpgh) { return std::make_unique<StatusParagraph>(test_origin, std::move(rpgh)); }));

    auto it = status_db.find_installed({"ffmpeg", Test::X64_WINDOWS});
    REQUIRE(it != status_db.end());

    // Feature "openssl" is not installed and should not be found
    auto it1 = status_db.find_installed({{"ffmpeg", Test::X64_WINDOWS}, "openssl"});
    REQUIRE(it1 == status_db.end());
}

TEST_CASE ("find for feature packages", "[statusparagraphs]")
{
    auto pghs = parse_paragraphs(R"(
Package: ffmpeg
Version: 3.3.3
Architecture: x64-windows
Multi-Arch: same
Description:
Status: install ok installed

Package: ffmpeg
Feature: openssl
Depends: openssl
Architecture: x64-windows
Multi-Arch: same
Description:
Status: install ok installed
)",
                                 "test-origin");
    REQUIRE(pghs);

    StatusParagraphs status_db(Util::fmap(
        *pghs.get(), [](Paragraph& rpgh) { return std::make_unique<StatusParagraph>(test_origin, std::move(rpgh)); }));

    // Feature "openssl" is installed and should therefore be found
    auto it = status_db.find_installed({{"ffmpeg", Test::X64_WINDOWS}, "openssl"});
    REQUIRE(it != status_db.end());
}
