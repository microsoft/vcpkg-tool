#include <catch2/catch.hpp>

#include <vcpkg/base/files.h>

#include <vcpkg/commands.extract.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <limits.h>

#include <vcpkg-test/util.h>

using namespace vcpkg;
using namespace vcpkg::Commands;

/*
* C:\
|__to
    |__ path
        |__ archive
        |   |__ folder1
        |   |   |__ file1.txt
        |   |   |__ file2.txt
        |   |   |__ file3.txt
        |   |___folder2
        |       |__ file4.txt
        |       |__ file5.txt
        |       |__ folder3
        |           |__ file6.txt
        |           |__ file7.txt
        |__ . . .
*/

#if defined(_WIN32)
const Path BASE_TEMP_PATH = "C:\\to\\path\\temp\\";
const Path BASE_PATH = "C:\\to\\path\\";
#else
const Path BASE_TEMP_PATH = "/to/path/temp/";
const Path BASE_PATH = "/to/path/";
#endif

ExtractedArchive archive = {BASE_TEMP_PATH,
                            BASE_PATH,
                            {
                                "archive" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file1.txt",
                                "archive" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file2.txt",
                                "archive" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file3.txt",
                                "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR "file4.txt",
                                "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR "file5.txt",
                                "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                                "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt",
                                "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                                "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt",
                            }};

const Path FILE_1 = {BASE_TEMP_PATH + "archive" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR
                                      "file1.txt"};
const Path FILE_2 = {BASE_TEMP_PATH + "archive" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR
                                      "file2.txt"};
const Path FILE_3 = {BASE_TEMP_PATH + "archive" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR
                                      "file3.txt"};
const Path FILE_4 = {BASE_TEMP_PATH + "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                                      "file4.txt"};
const Path FILE_5 = {BASE_TEMP_PATH + "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                                      "file5.txt"};
const Path FILE_6 = {BASE_TEMP_PATH + "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                                      "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt"};
const Path FILE_7 = {BASE_TEMP_PATH + "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                                      "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt"};

static void test_strip_map(const int strip, const std::vector<std::pair<Path, Path>>& expected)
{
    auto map = strip_map(archive, strip);
    REQUIRE(map.size() == expected.size());
    for (size_t i = 0; i < map.size(); ++i)
    {
        REQUIRE(map[i].first.native() == expected[i].first.native());
        REQUIRE(map[i].second.native() == expected[i].second.native());
    }
}

static void test_auto_strip_count(const std::vector<Path>& paths, size_t expected) 
{
    REQUIRE(get_auto_strip_count(paths) == expected);
}

TEST_CASE ("Testing strip_map, strip = 1", "[z-extract]")
{
    std::vector<std::pair<Path, Path>> expected = {
        {FILE_1, BASE_PATH + "folder1" VCPKG_PREFERRED_SEPARATOR "file1.txt"},
        {FILE_2, BASE_PATH + "folder1" VCPKG_PREFERRED_SEPARATOR "file2.txt"},
        {FILE_3, BASE_PATH + "folder1" VCPKG_PREFERRED_SEPARATOR "file3.txt"},
        {FILE_4, BASE_PATH + "folder2" VCPKG_PREFERRED_SEPARATOR "file4.txt"},
        {FILE_5, BASE_PATH + "folder2" VCPKG_PREFERRED_SEPARATOR "file5.txt"},
        {FILE_6, BASE_PATH + "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt"},
        {FILE_7, BASE_PATH + "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt"}};

    test_strip_map(1, expected);
}

TEST_CASE ("Testing strip_map, strip = 2", "[z-extract]")
{
    std::vector<std::pair<Path, Path>> expected = {
        {FILE_1, BASE_PATH + "file1.txt"},
        {FILE_2, BASE_PATH + "file2.txt"},
        {FILE_3, BASE_PATH + "file3.txt"},
        {FILE_4, BASE_PATH + "file4.txt"},
        {FILE_5, BASE_PATH + "file5.txt"},
        {FILE_6, BASE_PATH + "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt"},
        {FILE_7, BASE_PATH + "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt"}};

    test_strip_map(2, expected);
}

TEST_CASE ("Testing strip_map, strip = 3 (Max archive depth)", "[z-extract]")
{
    std::vector<std::pair<Path, Path>> expected = {{FILE_1, ""},
                                                   {FILE_2, ""},
                                                   {FILE_3, ""},
                                                   {FILE_4, ""},
                                                   {FILE_5, ""},
                                                   {FILE_6, BASE_PATH + "file6.txt"},
                                                   {FILE_7, BASE_PATH + "file7.txt"}};

    test_strip_map(3, expected);
}

TEST_CASE ("Testing strip_map, strip = AUTO => remove all common prefixes from path", "z-extract")
{
    std::vector<std::pair<Path, Path>> expected = {
        {FILE_1, BASE_PATH + "folder1" VCPKG_PREFERRED_SEPARATOR "file1.txt"},
        {FILE_2, BASE_PATH + "folder1" VCPKG_PREFERRED_SEPARATOR "file2.txt"},
        {FILE_3, BASE_PATH + "folder1" VCPKG_PREFERRED_SEPARATOR "file3.txt"},
        {FILE_4, BASE_PATH + "folder2" VCPKG_PREFERRED_SEPARATOR "file4.txt"},
        {FILE_5, BASE_PATH + "folder2" VCPKG_PREFERRED_SEPARATOR "file5.txt"},
        {FILE_6, BASE_PATH + "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt"},
        {FILE_7, BASE_PATH + "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt"}};

    test_strip_map(-1, expected);
}

TEST_CASE ("Testing strip auto's get_auto_strip_count "
           "z-extract")
{
    std::vector<Path> expect_one = {"archive" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file1.txt",
                                    "archive" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file2.txt",
                                    "archive" VCPKG_PREFERRED_SEPARATOR "folder1" VCPKG_PREFERRED_SEPARATOR "file3.txt",
                                    "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR "file4.txt",
                                    "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR "file5.txt",
                                    "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                                    "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt",
                                    "archive" VCPKG_PREFERRED_SEPARATOR "folder2" VCPKG_PREFERRED_SEPARATOR
                                    "folder3" VCPKG_PREFERRED_SEPARATOR "file7.txt"};

    std::vector<Path> expect_zero = {"folder1" VCPKG_PREFERRED_SEPARATOR "file1.txt",
                                    "folder1" VCPKG_PREFERRED_SEPARATOR "file2.txt",
                                    "folder1" VCPKG_PREFERRED_SEPARATOR "file3.txt",
                                    "folder2" VCPKG_PREFERRED_SEPARATOR "file4.txt",
                                    "folder2" VCPKG_PREFERRED_SEPARATOR "file5.txt",
                                    "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR "file6.txt",
                                    "folder2" VCPKG_PREFERRED_SEPARATOR "folder3" VCPKG_PREFERRED_SEPARATOR
                                    "file7.txt"};

    test_auto_strip_count(expect_one, 1);
    test_auto_strip_count(expect_zero, 0);
}
