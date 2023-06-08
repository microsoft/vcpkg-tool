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
        |   |   |__file3.txt
        |   |___folder2
        |       |__ file4.txt
        |       |__ file5.txt
        |       |__ folder3
        |           |__ file6.txt
        |           |__ file7.txt
        |__ . . .

*/

#if defined(_WIN32)
const std::string DELIMITER = "\\";
const std::string BASE_PATH = "C:\\to\\path\\";
#else
const std::string DELIMITER = "/";
const std::string BASE_PATH = "/to/path/";
#endif

ExtractedArchive archive = {BASE_PATH,
                            {
                                "archive" + DELIMITER + "folder1" + DELIMITER + "file1.txt",
                                "archive" + DELIMITER + "folder1" + DELIMITER + "file2.txt",
                                "archive" + DELIMITER + "folder1" + DELIMITER + "file3.txt",
                                "archive" + DELIMITER + "folder2" + DELIMITER + "file4.txt",
                                "archive" + DELIMITER + "folder2" + DELIMITER + "file5.txt",
                                "archive" + DELIMITER + "folder2" + DELIMITER + "folder3" + DELIMITER + "file6.txt",
                                "archive" + DELIMITER + "folder2" + DELIMITER + "folder3" + DELIMITER + "file7.txt",
                            }};

const Path FILE_1 = {BASE_PATH + "archive" + DELIMITER + "folder1" + DELIMITER + "file1.txt"};
const Path FILE_2 = {BASE_PATH + "archive" + DELIMITER + "folder1" + DELIMITER + "file2.txt"};
const Path FILE_3 = {BASE_PATH + "archive" + DELIMITER + "folder1" + DELIMITER + "file3.txt"};
const Path FILE_4 = {BASE_PATH + "archive" + DELIMITER + "folder2" + DELIMITER + "file4.txt"};
const Path FILE_5 = {BASE_PATH + "archive" + DELIMITER + "folder2" + DELIMITER + "file5.txt"};
const Path FILE_6 = {BASE_PATH + "archive" + DELIMITER + "folder2" + DELIMITER + "folder3" + DELIMITER + "file6.txt"};
const Path FILE_7 = {BASE_PATH + "archive" + DELIMITER + "folder2" + DELIMITER + "folder3" + DELIMITER + "file7.txt"};

static void test_strip_map(const int strip, const std::vector<std::pair<Path, Path>>& expected)
{
    auto map = strip_map(archive, strip);
    REQUIRE(map.size() == expected.size());
    for (size_t i = 0; i < map.size(); ++i)
    {
        REQUIRE(map[i].first.generic_u8string() == expected[i].first.generic_u8string());
        REQUIRE(map[i].second.generic_u8string() == expected[i].second.generic_u8string());
    }
}

TEST_CASE ("Testing strip_map, strip = 1", "[z-extract]")
{
    std::vector<std::pair<Path, Path>> expected = {
        {FILE_1, BASE_PATH + "folder1" + DELIMITER + "file1.txt"},
        {FILE_2, BASE_PATH + "folder1" + DELIMITER + "file2.txt"},
        {FILE_3, BASE_PATH + "folder1" + DELIMITER + "file3.txt"},
        {FILE_4, BASE_PATH + "folder2" + DELIMITER + "file4.txt"},
        {FILE_5, BASE_PATH + "folder2" + DELIMITER + "file5.txt"},
        {FILE_6, BASE_PATH + "folder2" + DELIMITER + "folder3" + DELIMITER + "file6.txt"},
        {FILE_7, BASE_PATH + "folder2" + DELIMITER + "folder3" + DELIMITER + "file7.txt"}};

    test_strip_map(1, expected);
}

TEST_CASE ("Testing strip_map, strip = 2", "[z-extract]")
{
    std::vector<std::pair<Path, Path>> expected = {{FILE_1, BASE_PATH + "file1.txt"},
                                                   {FILE_2, BASE_PATH + "file2.txt"},
                                                   {FILE_3, BASE_PATH + "file3.txt"},
                                                   {FILE_4, BASE_PATH + "file4.txt"},
                                                   {FILE_5, BASE_PATH + "file5.txt"},
                                                   {FILE_6, BASE_PATH + "folder3" + DELIMITER + "file6.txt"},
                                                   {FILE_7, BASE_PATH + "folder3" + DELIMITER + "file7.txt"}};

    test_strip_map(2, expected);
}

TEST_CASE ("Testing strip_map, strip = 3 (Max archive depth)", "[z-extract]")
{
    std::vector<std::pair<Path, Path>> expected = {{FILE_1, BASE_PATH + "file1.txt"},
                                                   {FILE_2, BASE_PATH + "file2.txt"},
                                                   {FILE_3, BASE_PATH + "file3.txt"},
                                                   {FILE_4, BASE_PATH + "file4.txt"},
                                                   {FILE_5, BASE_PATH + "file5.txt"},
                                                   {FILE_6, BASE_PATH + "file6.txt"},
                                                   {FILE_7, BASE_PATH + "file7.txt"}};

    test_strip_map(3, expected);
}
