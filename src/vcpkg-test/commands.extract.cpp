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
ExtractedArchive archive = {"C:\\to\\path\\",
                            {
                                "archive\\folder1\\file1.txt",
                                "archive\\folder1\\file2.txt",
                                "archive\\folder1\\file3.txt",
                                "archive\\folder2\\file4.txt",
                                "archive\\folder2\\file5.txt",
                                "archive\\folder2\\folder3\\file6.txt",
                                "archive\\folder2\\folder3\\file7.txt",
                            }};

const Path FILE_1 = {"C:\\to\\path\\archive\\folder1\\file1.txt"};
const Path FILE_2 = {"C:\\to\\path\\archive\\folder1\\file2.txt"};
const Path FILE_3 = {"C:\\to\\path\\archive\\folder1\\file3.txt"};
const Path FILE_4 = {"C:\\to\\path\\archive\\folder2\\file4.txt"};
const Path FILE_5 = {"C:\\to\\path\\archive\\folder2\\file5.txt"};
const Path FILE_6 = {"C:\\to\\path\\archive\\folder2\\folder3\\file6.txt"};
const Path FILE_7 = {"C:\\to\\path\\archive\\folder2\\folder3\\file7.txt"};

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

TEST_CASE ("Testing strip_map, strip = 1", "[z-extract]")
{
    std::vector<std::pair<Path, Path>> expected = {{FILE_1, "C:\\to\\path\\folder1\\file1.txt"},
                                                   {FILE_2, "C:\\to\\path\\folder1\\file2.txt"},
                                                   {FILE_3, "C:\\to\\path\\folder1\\file3.txt"},
                                                   {FILE_4, "C:\\to\\path\\folder2\\file4.txt"},
                                                   {FILE_5, "C:\\to\\path\\folder2\\file5.txt"},
                                                   {FILE_6, "C:\\to\\path\\folder2\\folder3\\file6.txt"},
                                                   {FILE_7, "C:\\to\\path\\folder2\\folder3\\file7.txt"}};
    test_strip_map(1, expected);
}

TEST_CASE ("Testing strip_map, strip = 2", "[z-extract]")
{
    std::vector<std::pair<Path, Path>> expected = {{FILE_1, "C:\\to\\path\\file1.txt"},
                                                   {FILE_2, "C:\\to\\path\\file2.txt"},
                                                   {FILE_3, "C:\\to\\path\\file3.txt"},
                                                   {FILE_4, "C:\\to\\path\\file4.txt"},
                                                   {FILE_5, "C:\\to\\path\\file5.txt"},
                                                   {FILE_6, "C:\\to\\path\\folder3\\file6.txt"},
                                                   {FILE_7, "C:\\to\\path\\folder3\\file7.txt"}};

    test_strip_map(2, expected);
}

TEST_CASE ("Testing strip_map, strip = 3 (Max archive depth)", "[z-extract]")
{
    std::vector<std::pair<Path, Path>> expected = {{FILE_1, "C:\\to\\path\\file1.txt"},
                                                   {FILE_2, "C:\\to\\path\\file2.txt"},
                                                   {FILE_3, "C:\\to\\path\\file3.txt"},
                                                   {FILE_4, "C:\\to\\path\\file4.txt"},
                                                   {FILE_5, "C:\\to\\path\\file5.txt"},
                                                   {FILE_6, "C:\\to\\path\\file6.txt"},
                                                   {FILE_7, "C:\\to\\path\\file7.txt"}};

    test_strip_map(3, expected);
}
