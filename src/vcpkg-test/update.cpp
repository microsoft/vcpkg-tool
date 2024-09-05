#include <vcpkg-test/util.h>

#include <vcpkg/commands.update.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/statusparagraphs.h>

using namespace vcpkg;
using namespace vcpkg::Test;

using Pgh = std::vector<std::unordered_map<std::string, std::string>>;

TEST_CASE ("find outdated packages basic", "[update]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("a"));
    status_paragraphs.back()->package.version = Version{"2", 0};

    StatusParagraphs status_db(std::move(status_paragraphs));

    std::unordered_map<std::string, SourceControlFileAndLocation> map;
    auto scf = test_parse_control_file({{{"Source", "a"}, {"Version", "0"}}}).value(VCPKG_LINE_INFO);
    map.emplace("a", SourceControlFileAndLocation{std::move(scf), ""});
    MapPortFileProvider provider(map);

    auto outdated_report = build_outdated_report(provider, status_db);
    REQUIRE(outdated_report.up_to_date_packages.empty());
    auto& pkgs = outdated_report.outdated_packages;
    REQUIRE(pkgs.size() == 1);
    REQUIRE(pkgs[0].spec.to_string() == "a:x86-windows");
    REQUIRE(pkgs[0].version_diff.left.to_string() == "2");
    REQUIRE(pkgs[0].version_diff.right.to_string() == "0");
    REQUIRE(outdated_report.missing_packages.empty());
    REQUIRE(outdated_report.parse_errors.empty());
}

TEST_CASE ("find outdated packages features", "[update]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("a"));
    status_paragraphs.back()->package.version = Version{"2", 0};

    status_paragraphs.push_back(make_status_feature_pgh("a", "b"));
    status_paragraphs.back()->package.version = Version{"2", 0};

    StatusParagraphs status_db(std::move(status_paragraphs));

    std::unordered_map<std::string, SourceControlFileAndLocation> map;
    auto scf = test_parse_control_file({{{"Source", "a"}, {"Version", "0"}}}).value(VCPKG_LINE_INFO);
    map.emplace("a", SourceControlFileAndLocation{std::move(scf), ""});
    MapPortFileProvider provider(map);

    auto outdated_report = build_outdated_report(provider, status_db);
    REQUIRE(outdated_report.up_to_date_packages.empty());
    auto& pkgs = outdated_report.outdated_packages;
    REQUIRE(pkgs.size() == 1);
    REQUIRE(pkgs[0].spec.to_string() == "a:x86-windows");
    REQUIRE(pkgs[0].version_diff.left.to_string() == "2");
    REQUIRE(pkgs[0].version_diff.right.to_string() == "0");
    REQUIRE(outdated_report.missing_packages.empty());
    REQUIRE(outdated_report.parse_errors.empty());
}

TEST_CASE ("find outdated packages features 2", "[update]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("a"));
    status_paragraphs.back()->package.version = Version{"2", 0};

    status_paragraphs.push_back(make_status_feature_pgh("a", "b"));
    status_paragraphs.back()->package.version = Version{"0", 0};
    status_paragraphs.back()->state = InstallState::NOT_INSTALLED;
    status_paragraphs.back()->want = Want::PURGE;

    StatusParagraphs status_db(std::move(status_paragraphs));

    std::unordered_map<std::string, SourceControlFileAndLocation> map;
    auto scf = test_parse_control_file({{{"Source", "a"}, {"Version", "0"}}}).value(VCPKG_LINE_INFO);
    map.emplace("a", SourceControlFileAndLocation{std::move(scf), ""});
    MapPortFileProvider provider(map);

    auto outdated_report = build_outdated_report(provider, status_db);
    REQUIRE(outdated_report.up_to_date_packages.empty());
    auto& pkgs = outdated_report.outdated_packages;
    REQUIRE(pkgs.size() == 1);
    REQUIRE(pkgs[0].spec.to_string() == "a:x86-windows");
    REQUIRE(pkgs[0].version_diff.left.to_string() == "2");
    REQUIRE(pkgs[0].version_diff.right.to_string() == "0");
    REQUIRE(outdated_report.missing_packages.empty());
    REQUIRE(outdated_report.parse_errors.empty());
}

TEST_CASE ("find outdated packages missing and none", "[update]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("a"));
    status_paragraphs.back()->package.version = Version{"2", 0};
    status_paragraphs.push_back(make_status_pgh("b"));
    status_paragraphs.back()->package.version = Version{"6", 0};

    StatusParagraphs status_db(std::move(status_paragraphs));

    std::unordered_map<std::string, SourceControlFileAndLocation> map;
    auto scf = test_parse_control_file({{{"Source", "a"}, {"Version", "2"}}}).value(VCPKG_LINE_INFO);
    map.emplace("a", SourceControlFileAndLocation{std::move(scf), ""});
    MapPortFileProvider provider(map);

    auto outdated_report = build_outdated_report(provider, status_db);
    REQUIRE(outdated_report.up_to_date_packages.size() == 1);
    REQUIRE(outdated_report.up_to_date_packages[0].to_string() == "a:x86-windows@2");
    REQUIRE(outdated_report.outdated_packages.empty());
    REQUIRE(outdated_report.missing_packages.size() == 1);
    REQUIRE(outdated_report.missing_packages[0].to_string() == "b:x86-windows@6");
    REQUIRE(outdated_report.parse_errors.empty());
}
