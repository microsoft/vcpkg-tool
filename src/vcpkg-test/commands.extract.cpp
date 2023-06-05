#include <catch2/catch.hpp>

#include <vcpkg/base/files.h>

#include <vcpkg/commands.extract.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <limits.h>

#include <vcpkg-test/util.h>

using namespace vcpkg;
using namespace vcpkg::Commands;

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

TEST_CASE ("Testing strip_map, strip = 1", "[z-extract]")
{

    std::vector<std::pair<Path, Path>> results = {
        {"C:\\to\\path\\archive\\folder1\\file1.txt", "C:\\to\\path\\folder1\\file1.txt"},
        {"C:\\to\\path\\archive\\folder1\\file2.txt", "C:\\to\\path\\folder1\\file2.txt"},
        {"C:\\to\\path\\archive\\folder1\\file3.txt", "C:\\to\\path\\folder1\\file3.txt"},
        {"C:\\to\\path\\archive\\folder2\\file4.txt", "C:\\to\\path\\folder2\\file4.txt"},
        {"C:\\to\\path\\archive\\folder2\\file5.txt", "C:\\to\\path\\folder2\\file5.txt"},
        {"C:\\to\\path\\archive\\folder2\\folder3\\file6.txt", "C:\\to\\path\\folder2\\folder3\\file6.txt"},
        {"C:\\to\\path\\archive\\folder2\\folder3\\file7.txt", "C:\\to\\path\\folder2\\folder3\\file7.txt"}};

    auto map = strip_map(archive, 1);
    REQUIRE(map.size() == results.size());

    for (size_t i = 0; i < map.size(); ++i)
    {
        REQUIRE(map[i].first.native() == results[i].first.native());
        REQUIRE(map[i].second.native() == results[i].second.native());
    }
}

TEST_CASE ("Testing strip_map, strip = 2", "[z-extract]")
{
    std::vector<std::pair<Path, Path>> results = {
        {"C:\\to\\path\\archive\\folder1\\file1.txt", "C:\\to\\path\\file1.txt"},
        {"C:\\to\\path\\archive\\folder1\\file2.txt", "C:\\to\\path\\file2.txt"},
        {"C:\\to\\path\\archive\\folder1\\file3.txt", "C:\\to\\path\\file3.txt"},
        {"C:\\to\\path\\archive\\folder2\\file4.txt", "C:\\to\\path\\file4.txt"},
        {"C:\\to\\path\\archive\\folder2\\file5.txt", "C:\\to\\path\\file5.txt"},
        {"C:\\to\\path\\archive\\folder2\\folder3\\file6.txt", "C:\\to\\path\\folder3\\file6.txt"},
        {"C:\\to\\path\\archive\\folder2\\folder3\\file7.txt", "C:\\to\\path\\folder3\\file7.txt"}};

    auto map = strip_map(archive, 2);
    REQUIRE(map.size() == results.size());

    for (size_t i = 0; i < map.size(); ++i)
    {
        REQUIRE(map[i].first.native() == results[i].first.native());
        REQUIRE(map[i].second.native() == results[i].second.native());
    }
}

TEST_CASE ("Testing strip_map, strip = 3 (Max archive depth)", "[z-extract]")
{
    std::vector<std::pair<Path, Path>> results = {
        {"C:\\to\\path\\archive\\folder1\\file1.txt", "C:\\to\\path\\file1.txt"},
        {"C:\\to\\path\\archive\\folder1\\file2.txt", "C:\\to\\path\\file2.txt"},
        {"C:\\to\\path\\archive\\folder1\\file3.txt", "C:\\to\\path\\file3.txt"},
        {"C:\\to\\path\\archive\\folder2\\file4.txt", "C:\\to\\path\\file4.txt"},
        {"C:\\to\\path\\archive\\folder2\\file5.txt", "C:\\to\\path\\file5.txt"},
        {"C:\\to\\path\\archive\\folder2\\folder3\\file6.txt", "C:\\to\\path\\file6.txt"},
        {"C:\\to\\path\\archive\\folder2\\folder3\\file7.txt", "C:\\to\\path\\file7.txt"}};

    auto map = strip_map(archive, 3);
    REQUIRE(map.size() == results.size());

    for (size_t i = 0; i < map.size(); ++i)
    {
        REQUIRE(map[i].first.native() == results[i].first.native());
        REQUIRE(map[i].second.native() == results[i].second.native());
    }
}

