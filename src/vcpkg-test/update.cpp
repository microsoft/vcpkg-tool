#include <vcpkg-test/util.h>

#include <vcpkg/base/util.h>

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

    auto pkgs = find_outdated_packages(provider, status_db);
    Util::sort(pkgs, &OutdatedPackage::compare_by_name);

    REQUIRE(pkgs.size() == 1);
    REQUIRE(pkgs[0].version_diff.left.to_string() == "2");
    REQUIRE(pkgs[0].version_diff.right.to_string() == "0");
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

    auto pkgs = find_outdated_packages(provider, status_db);
    Util::sort(pkgs, &OutdatedPackage::compare_by_name);

    REQUIRE(pkgs.size() == 1);
    REQUIRE(pkgs[0].version_diff.left.to_string() == "2");
    REQUIRE(pkgs[0].version_diff.right.to_string() == "0");
}

TEST_CASE ("find outdated packages features 2", "[update]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("a"));
    status_paragraphs.back()->package.version = Version{"2", 0};

    status_paragraphs.push_back(make_status_feature_pgh("a", "b"));
    status_paragraphs.back()->package.version = Version{"0", 0};
    status_paragraphs.back()->status = {Want::PURGE, InstallState::NOT_INSTALLED};

    StatusParagraphs status_db(std::move(status_paragraphs));

    std::unordered_map<std::string, SourceControlFileAndLocation> map;
    auto scf = test_parse_control_file({{{"Source", "a"}, {"Version", "0"}}}).value(VCPKG_LINE_INFO);
    map.emplace("a", SourceControlFileAndLocation{std::move(scf), ""});
    MapPortFileProvider provider(map);

    auto pkgs = find_outdated_packages(provider, status_db);
    Util::sort(pkgs, &OutdatedPackage::compare_by_name);

    REQUIRE(pkgs.size() == 1);
    REQUIRE(pkgs[0].version_diff.left.to_string() == "2");
    REQUIRE(pkgs[0].version_diff.right.to_string() == "0");
}

TEST_CASE ("find outdated packages none", "[update]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("a"));
    status_paragraphs.back()->package.version = Version{"2", 0};

    StatusParagraphs status_db(std::move(status_paragraphs));

    std::unordered_map<std::string, SourceControlFileAndLocation> map;
    auto scf = test_parse_control_file({{{"Source", "a"}, {"Version", "2"}}}).value(VCPKG_LINE_INFO);
    map.emplace("a", SourceControlFileAndLocation{std::move(scf), ""});
    MapPortFileProvider provider(map);

    auto pkgs = find_outdated_packages(provider, status_db);
    REQUIRE(pkgs.size() == 0);
}
