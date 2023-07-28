#include <catch2/catch.hpp>

#include <vcpkg/base/files.h>

#include <vcpkg/commands.extract.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <limits.h>

#include <vcpkg-test/util.h>

using namespace vcpkg;
using namespace vcpkg::Commands;

#if defined(_WIN32)
#define BASE_TEMP_PATH "C:\\to\\path\\temp\\"
#define BASE_PATH "C:\\to\\path\\"
#else // ^^^ _WIN32 // !_WIN32 vvv
#define BASE_TEMP_PATH "/to/path/temp/"
#define BASE_PATH "/to/path/"
#endif // ^^^ !_WIN32

// C:
// |__to
//     |__ path
//         |__ folder0
#define ARCHIVE_PATH BASE_TEMP_PATH "folder0"
//         |   |__ folder1
#define FOLDER_1 ARCHIVE_PATH VCPKG_PREFERRED_SEPARATOR "folder1"
//         |   |   |__ file1.txt
#define FILE_1 FOLDER_1 VCPKG_PREFERRED_SEPARATOR "file1.txt"
//         |   |   |__ file2.txt
#define FILE_2 FOLDER_1 VCPKG_PREFERRED_SEPARATOR "file2.txt"
//         |   |   |__ file3.txt
#define FILE_3 FOLDER_1 VCPKG_PREFERRED_SEPARATOR "file3.txt"
//         |   |___folder2
#define FOLDER_2 ARCHIVE_PATH VCPKG_PREFERRED_SEPARATOR "folder2"
//         |       |__ file4.txt
#define FILE_4 FOLDER_2 VCPKG_PREFERRED_SEPARATOR "file4.txt"
//         |       |__ file5.txt
#define FILE_5 FOLDER_2 VCPKG_PREFERRED_SEPARATOR "file5.txt"
//         |       |__ folder3
#define FOLDER_3 FOLDER_2 VCPKG_PREFERRED_SEPARATOR "folder3"
//         |           |__ file6.txt
#define FILE_6 FOLDER_3 VCPKG_PREFERRED_SEPARATOR "file6.txt"
//         |           |__ file7.txt
#define FILE_7 FOLDER_3 VCPKG_PREFERRED_SEPARATOR "file7.txt"
//         |__ . . .

ExtractedArchive archive = {BASE_TEMP_PATH,
                            BASE_PATH,
                            {
                                "folder0" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file1.txt",
                                "folder0" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file2.txt",
                                "folder0" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file3.txt",
                                "folder0" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR "file4.txt",
                                "folder0" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR "file5.txt",
                                "folder0" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                                "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt",
                                "folder0" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                                "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt",
                            }};

TEST_CASE ("Testing strip_map, strip = 1", "[z-extract]")
{
    REQUIRE(
        get_archive_deploy_operations(archive, {StripMode::Manual, 1}) ==
        std::vector<std::pair<Path, Path>>{
            {FILE_1, BASE_PATH "folder1" VCPKG_PREFERRED_SEPARATOR "file1.txt"},
            {FILE_2, BASE_PATH "folder1" VCPKG_PREFERRED_SEPARATOR "file2.txt"},
            {FILE_3, BASE_PATH "folder1" VCPKG_PREFERRED_SEPARATOR "file3.txt"},
            {FILE_4, BASE_PATH "folder2" VCPKG_PREFERRED_SEPARATOR "file4.txt"},
            {FILE_5, BASE_PATH "folder2" VCPKG_PREFERRED_SEPARATOR "file5.txt"},
            {FILE_6, BASE_PATH "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt"},
            {FILE_7, BASE_PATH "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt"}});
}

TEST_CASE ("Testing strip_map, strip = 2", "[z-extract]")
{
    REQUIRE(get_archive_deploy_operations(archive, {StripMode::Manual, 2}) ==
            std::vector<std::pair<Path, Path>>{{FILE_1, BASE_PATH "file1.txt"},
                                               {FILE_2, BASE_PATH "file2.txt"},
                                               {FILE_3, BASE_PATH "file3.txt"},
                                               {FILE_4, BASE_PATH "file4.txt"},
                                               {FILE_5, BASE_PATH "file5.txt"},
                                               {FILE_6, BASE_PATH "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt"},
                                               {FILE_7, BASE_PATH "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt"}});
}

TEST_CASE ("Testing strip_map, strip = 3 (Max archive depth)", "[z-extract]")
{
    REQUIRE(get_archive_deploy_operations(archive, {StripMode::Manual, 3}) ==
            std::vector<std::pair<Path, Path>>{{FILE_6, BASE_PATH "file6.txt"}, {FILE_7, BASE_PATH "file7.txt"}});
}

TEST_CASE ("Testing strip_map, strip = AUTO => remove all common prefixes from path", "z-extract")
{
    REQUIRE(
        get_archive_deploy_operations(archive, {StripMode::Automatic, -1}) ==
        std::vector<std::pair<Path, Path>>{
            {FILE_1, BASE_PATH "folder1" VCPKG_PREFERRED_SEPARATOR "file1.txt"},
            {FILE_2, BASE_PATH "folder1" VCPKG_PREFERRED_SEPARATOR "file2.txt"},
            {FILE_3, BASE_PATH "folder1" VCPKG_PREFERRED_SEPARATOR "file3.txt"},
            {FILE_4, BASE_PATH "folder2" VCPKG_PREFERRED_SEPARATOR "file4.txt"},
            {FILE_5, BASE_PATH "folder2" VCPKG_PREFERRED_SEPARATOR "file5.txt"},
            {FILE_6, BASE_PATH "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt"},
            {FILE_7, BASE_PATH "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt"}});
}

TEST_CASE ("Testing strip auto's get_common_prefix_count", "z-extract")
{
    REQUIRE(1 == get_common_directories_count(
                     {"folder0" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file1.txt",
                      "folder0" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file2.txt",
                      "folder0" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file3.txt",
                      "folder0" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR "file4.txt",
                      "folder0" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR "file5.txt",
                      "folder0" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                      "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt",
                      "folder0" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                      "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt"}));

    REQUIRE(0 == get_common_directories_count(
                     {"folder1" VCPKG_PREFERRED_SEPARATOR "file1.txt",
                      "folder1" VCPKG_PREFERRED_SEPARATOR "file2.txt",
                      "folder1" VCPKG_PREFERRED_SEPARATOR "file3.txt",
                      "folder2" VCPKG_PREFERRED_SEPARATOR "file4.txt",
                      "folder2" VCPKG_PREFERRED_SEPARATOR "file5.txt",
                      "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt",
                      "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt"}));

    REQUIRE(0 == get_common_directories_count({}));
    REQUIRE(0 == get_common_directories_count({"file1.txt", "file2.txt"}));
    REQUIRE(0 == get_common_directories_count({"file1.txt"}));
    REQUIRE(1 == get_common_directories_count({"folder1" VCPKG_PREFERRED_SEPARATOR "file1.txt"}));
}

TEST_CASE ("Testing get_strip_setting", "z-extract")
{
    std::map<std::string, std::string, std::less<>> settings;

    SECTION ("Test no strip")
    {
        REQUIRE(StripSetting{StripMode::Manual, 0} == get_strip_setting(settings).value_or_exit(VCPKG_LINE_INFO));
    }

    SECTION("Test Manual strip with count of 1")
    {
		settings["strip"] = "1";
        REQUIRE(StripSetting{StripMode::Manual, 1} == get_strip_setting(settings).value_or_exit(VCPKG_LINE_INFO));
        settings.clear();
	}

    SECTION ("Test Manual strip with count greater than 1")
    {
        settings["strip"] = "5000";
        REQUIRE(StripSetting{StripMode::Manual, 5} == get_strip_setting(settings).value_or_exit(VCPKG_LINE_INFO));
        settings.clear();
    }

    SECTION ("Test Automatic strip")
    {
        settings["strip"] = "auto";
        REQUIRE(StripSetting{StripMode::Automatic, -1} == get_strip_setting(settings).value_or_exit(VCPKG_LINE_INFO));
        settings.clear();
        settings["strip"] = "AUTO";
        REQUIRE(StripSetting{StripMode::Automatic, -1} == get_strip_setting(settings).value_or_exit(VCPKG_LINE_INFO));
        settings.clear();
        settings["strip"] = "AuTo";
        REQUIRE(StripSetting{StripMode::Automatic, -1} == get_strip_setting(settings).value_or_exit(VCPKG_LINE_INFO));
        settings.clear();
    }

    SECTION ("Bad strip values rejected")
    {
        settings["strip"] = "-42";
        auto answer = get_strip_setting(settings);
        REQUIRE(!answer);
        REQUIRE(answer.error() ==
                LocalizedString::from_raw("error: --strip must be set to a nonnegative integer or 'AUTO'."));
        settings.clear();
    }
}
