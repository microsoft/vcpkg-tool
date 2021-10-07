#include <vcpkg/base/system_headers.h>

#include <catch2/catch.hpp>

#include <vcpkg/base/files.h>
#include <vcpkg/base/strings.h>

#include <iostream>
#include <random>
#include <vector>

#include <vcpkg-test/util.h>

using namespace vcpkg;
using Test::base_temporary_directory;

#define CHECK_EC_ON_FILE(file, ec)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (ec)                                                                                                        \
        {                                                                                                              \
            FAIL((file).native() << ": " << (ec).message());                                                           \
        }                                                                                                              \
    } while (0)

namespace
{
    using urbg_t = std::mt19937_64;

    std::string get_random_filename(urbg_t& urbg) { return Strings::b32_encode(urbg()); }

#if defined(_WIN32)
    bool is_valid_symlink_failure(const std::error_code& ec) noexcept
    {
        // on Windows, creating symlinks requires admin rights, so we ignore such failures
        return ec == std::error_code(ERROR_PRIVILEGE_NOT_HELD, std::system_category());
    }
#endif // ^^^ _WIN32

    void create_directory_tree(urbg_t& urbg, Filesystem& fs, const Path& base, std::uint32_t remaining_depth = 5)
    {
        using uid_t = std::uniform_int_distribution<std::uint32_t>;
        // we want ~70% of our "files" to be directories, and then a third
        // each of the remaining ~30% to be regular files, directory symlinks,
        // and regular symlinks
        constexpr std::uint32_t directory_min_tag = 0;
        constexpr std::uint32_t regular_file_tag = 7;
        constexpr std::uint32_t regular_symlink_tag = 8;
        constexpr std::uint32_t directory_symlink_tag = 9;

        std::uint32_t file_type;
        if (remaining_depth <= 1)
        {
            // if we're at the max depth, we only want to create non-directories
            file_type = uid_t{regular_file_tag, directory_symlink_tag}(urbg);
        }
        else if (remaining_depth >= 3)
        {
            // if we are far away from the max depth, always create directories
            // to make reaching the max depth likely
            file_type = directory_min_tag;
        }
        else
        {
            file_type = uid_t{directory_min_tag, regular_symlink_tag}(urbg);
        }

        std::error_code ec;
        if (file_type == regular_symlink_tag)
        {
            // regular symlink
            auto base_target = base + "-target";
            fs.write_contents(base_target, "", ec);
            CHECK_EC_ON_FILE(base_target, ec);
            fs.create_symlink(base_target, base, ec);

            if (ec)
            {
#if defined(_WIN32)
                if (is_valid_symlink_failure(ec))
                {
                    fs.write_contents(base, "", ec);
                    CHECK_EC_ON_FILE(base, ec);
                }
                else
#endif // ^^^ _WIN32
                {
                    FAIL(base.native() << ": " << ec.message());
                }
            }
        }
        else if (file_type == directory_symlink_tag)
        {
            // directory symlink
            auto parent = base;
            parent.remove_filename();
            fs.create_directory_symlink(parent, base, ec);
            if (ec)
            {
#if defined(_WIN32)
                if (is_valid_symlink_failure(ec))
                {
                    fs.create_directory(base, ec);
                    CHECK_EC_ON_FILE(base, ec);
                }
                else
#endif // ^^^ _WIN32
                {
                    FAIL(base.native() << ": " << ec.message());
                }
            }
        }
        else if (file_type == regular_file_tag)
        {
            // regular file
            fs.write_contents(base, "", ec);
            CHECK_EC_ON_FILE(base, ec);
        }
        else
        {
            // regular directory
            fs.create_directory(base, ec);
            CHECK_EC_ON_FILE(base, ec);
            for (int i = 0; i < 5; ++i)
            {
                create_directory_tree(urbg, fs, base / get_random_filename(urbg), remaining_depth - 1);
            }
        }

        REQUIRE(exists(fs.symlink_status(base, ec)));
        CHECK_EC_ON_FILE(base, ec);
    }

    Filesystem& setup()
    {
        auto& fs = get_real_filesystem();

        std::error_code ec;
        fs.create_directory(base_temporary_directory(), ec);
        CHECK_EC_ON_FILE(base_temporary_directory(), ec);

        return fs;
    }

    template<class Enumerator, class ExpectedGenerator>
    void do_filesystem_enumeration_test(Enumerator&& enumerator, ExpectedGenerator&& generate_expected)
    {
        urbg_t urbg;

        auto& fs = setup();

        auto temp_dir = base_temporary_directory() / get_random_filename(urbg);
        INFO("temp dir is: " << temp_dir.native());

        const auto target_root = temp_dir / "target";

        const auto target_file = target_root / "file.txt";
        const auto target_symlink = target_root / "symlink-to-file.txt";
        const auto target_directory = target_root / "some-directory";
        const auto target_directory_symlink = target_root / "symlink-to-some-directory";

        const auto target_inside_file = target_directory / "file2.txt";
        const auto target_inside_symlink = target_directory / "symlink-to-file2.txt";
        const auto target_inside_directory = target_directory / "some-inner-directory";
        const auto target_inside_directory_symlink = target_directory / "symlink-to-some-inner-directory";

        fs.create_directory(temp_dir, VCPKG_LINE_INFO);
        fs.create_directory(target_root, VCPKG_LINE_INFO);
        fs.create_directory(target_directory, VCPKG_LINE_INFO);
        fs.create_directory(target_inside_directory, VCPKG_LINE_INFO);

        fs.write_contents(target_file, "file", VCPKG_LINE_INFO);
        fs.write_contents(target_inside_file, "file in directory", VCPKG_LINE_INFO);

        std::error_code ec;
        fs.create_symlink(target_file, target_symlink, ec);
        if (ec)
        {
            // if we get not supported or permission denied, assume symlinks aren't supported
            // on this system and the test is a no-op
            REQUIRE((ec == std::errc::not_supported || ec == std::errc::permission_denied));
        }
        else
        {
            fs.create_symlink(target_inside_file, target_inside_symlink, VCPKG_LINE_INFO);
            fs.create_directory_symlink(target_directory, target_directory_symlink, VCPKG_LINE_INFO);
            fs.create_directory_symlink(target_inside_directory, target_inside_directory_symlink, VCPKG_LINE_INFO);

            auto results = std::forward<Enumerator>(enumerator)(fs, target_root);
            std::sort(results.begin(), results.end());
            auto expected = std::forward<ExpectedGenerator>(generate_expected)(target_root);
            REQUIRE(results == expected);
        }

        fs.remove_all(temp_dir, VCPKG_LINE_INFO);
    }
}

TEST_CASE ("vcpkg Path regular operations", "[filesystem][files]")
{
    CHECK(Path().native().empty());
    Path p("hello");
    CHECK(p == "hello");
    CHECK(p.native() == "hello");
    Path copy_constructed(p);
    CHECK(copy_constructed == "hello");
    CHECK(copy_constructed.native() == "hello");
    Path move_constructed(std::move(p));
    CHECK(move_constructed == "hello");
    CHECK(move_constructed.native() == "hello");

    p = "world";
    Path copy_assigned;
    copy_assigned = p;
    CHECK(copy_assigned == "world");
    CHECK(copy_assigned.native() == "world");

    Path move_assigned;
    move_assigned = std::move(p);
    CHECK(move_assigned == "world");
    CHECK(move_assigned.native() == "world");
}

TEST_CASE ("vcpkg Path conversions", "[filesystem][files]")
{
    StringLiteral sl("some literal");
    StringView sv = sl;
    std::string str("some string");
    std::string moved_from("moved from");
    const char* ntbs = "some utf-8";
    CHECK(Path(sv).native() == "some literal");
    CHECK(Path(str).native() == "some string");
    CHECK(Path(std::move(moved_from)).native() == "moved from");
    CHECK(Path(ntbs).native() == "some utf-8");
    CHECK(Path(str.begin(), str.end()).native() == "some string");
    CHECK(Path(str.data(), str.size()).native() == "some string");

    Path p("convert from");
    StringView conv_sv = p;
    CHECK(conv_sv == "convert from");
    CHECK(strcmp(p.c_str(), "convert from") == 0);
}

TEST_CASE ("vcpkg Path generic", "[filesystem][files]")
{
    Path p("some/path/with/forward/slashes");
    CHECK(p.generic_u8string() == StringView("some/path/with/forward/slashes"));

    Path p_dup("some/path/with//////duplicate//////////forward/slashes");
    CHECK(p_dup.generic_u8string() == StringView("some/path/with//////duplicate//////////forward/slashes"));

    Path bp("some\\path\\/\\/with\\backslashes");
#if defined(_WIN32)
    CHECK(bp.generic_u8string() == StringView("some/path////with/backslashes"));
#else  // ^^^ _WIN32 / !_WIN32 vvv
    CHECK(bp.generic_u8string() == StringView("some\\path\\/\\/with\\backslashes"));
#endif // _WIN32
}

static void test_op_slash(StringView base, StringView append, StringView expected)
{
    Path an_lvalue(base);
    CHECK((an_lvalue / append).native() == expected);  // Path operator/(StringView sv) const&;
    CHECK((Path(base) / append).native() == expected); // Path operator/(StringView sv) &&;
    an_lvalue /= append;                               // Path& operator/=(StringView sv);
    CHECK(an_lvalue.native() == expected);
}

TEST_CASE ("vcpkg Path::operator/", "[filesystem][files]")
{
    test_op_slash("/a/b", "c/d", "/a/b" VCPKG_PREFERRED_SEPARATOR "c/d");
    test_op_slash("a/b", "c/d", "a/b" VCPKG_PREFERRED_SEPARATOR "c/d");
    test_op_slash("/a/b", "/c/d", "/c/d");

#if defined(_WIN32)
    test_op_slash("C:/a/b", "c/d", "C:/a/b\\c/d");
    test_op_slash("C:a/b", "c/d", "C:a/b\\c/d");
    test_op_slash("C:a/b", "/c/d", "C:/c/d");
    test_op_slash("C:/a/b", "/c/d", "C:/c/d");
    test_op_slash("C:/a/b", "D:/c/d", "D:/c/d");
    test_op_slash("C:/a/b", "D:c/d", "D:c/d");
    test_op_slash("C:/a/b", "C:c/d", "C:/a/b\\c/d");
#else  // ^^^ _WIN32 / !_WIN32 vvv
    test_op_slash("C:/a/b", "c/d", "C:/a/b/c/d");
    test_op_slash("C:a/b", "c/d", "C:a/b/c/d");
    test_op_slash("C:a/b", "/c/d", "/c/d");
    test_op_slash("C:/a/b", "/c/d", "/c/d");
    test_op_slash("C:/a/b", "D:/c/d", "C:/a/b/D:/c/d");
    test_op_slash("C:/a/b", "D:c/d", "C:/a/b/D:c/d");
    test_op_slash("C:/a/b", "C:c/d", "C:/a/b/C:c/d");
#endif // ^^^ !_WIN32
}

static void test_op_plus(StringView base, StringView append)
{
    auto expected = base.to_string() + append.to_string();
    Path an_lvalue(base);
    CHECK((an_lvalue + append).native() == expected);  // Path operator+(StringView sv) const&;
    CHECK((Path(base) + append).native() == expected); // Path operator+(StringView sv) &&;
    an_lvalue += append;                               // Path& operator+=(StringView sv);
    CHECK(an_lvalue.native() == expected);
}

TEST_CASE ("vcpkg Path::operator+", "[filesystem][files]")
{
    test_op_plus("/a/b", "c/d");
    test_op_plus("a/b", "c/d");
    test_op_plus("/a/b", "/c/d");
    test_op_plus("C:/a/b", "c/d");
    test_op_plus("C:a/b", "c/d");
    test_op_plus("C:a/b", "/c/d");
    test_op_plus("C:/a/b", "/c/d");
    test_op_plus("C:/a/b", "D:/c/d");
    test_op_plus("C:/a/b", "D:c/d");
    test_op_plus("C:/a/b", "C:c/d");
}

static void test_preferred(Path p, StringView expected)
{
    p.make_preferred();
    CHECK(p.native() == expected);
}

TEST_CASE ("vcpkg Path::preferred and Path::make_preferred", "[filesystem][files]")
{
    test_preferred("", "");
    test_preferred("hello", "hello");
    test_preferred("/hello", VCPKG_PREFERRED_SEPARATOR "hello");
    test_preferred("hello/", "hello" VCPKG_PREFERRED_SEPARATOR);
    test_preferred("hello/////////there", "hello" VCPKG_PREFERRED_SEPARATOR "there");
    test_preferred("hello/////////there///" VCPKG_PREFERRED_SEPARATOR "world",
                   "hello" VCPKG_PREFERRED_SEPARATOR "there" VCPKG_PREFERRED_SEPARATOR "world");
    test_preferred("/a/b", VCPKG_PREFERRED_SEPARATOR "a" VCPKG_PREFERRED_SEPARATOR "b");
    test_preferred("a/b", "a" VCPKG_PREFERRED_SEPARATOR "b");

#if defined(_WIN32)
    test_preferred(R"(\\server/share\a/b)", R"(\\server\share\a\b)");
    test_preferred(R"(//server/share\a/b)", R"(\\server\share\a\b)");
#else  // ^^^ _WIN32 / !_WIN32 vvv
    test_preferred(R"(//server/share\a/b)", R"(/server/share\a/b)");
    test_preferred(R"(//server/share\a/b)", R"(/server/share\a/b)");
#endif // ^^^ !_WIN32
}

static void test_lexically_normal(Path p, Path expected_generic)
{
    auto as_lexically_normal = p.lexically_normal();
    expected_generic.make_preferred(); // now expected
    CHECK(as_lexically_normal.native() == expected_generic.native());
}

TEST_CASE ("Path::lexically_normal", "[filesystem][files]")
{
    test_lexically_normal({}, {});

    // these test cases are taken from the MS STL tests
    test_lexically_normal("cat/./dog/..", "cat/");
    test_lexically_normal("cat/.///dog/../", "cat/");

    test_lexically_normal("cat/./dog/..", "cat/");
    test_lexically_normal("cat/.///dog/../", "cat/");

    test_lexically_normal(".", ".");
    test_lexically_normal("./", ".");
    test_lexically_normal("./.", ".");
    test_lexically_normal("././", ".");

    test_lexically_normal("../../..", "../../..");
    test_lexically_normal("../../../", "../../..");

    test_lexically_normal("../../../a/b/c", "../../../a/b/c");

    test_lexically_normal("/../../..", "/");
    test_lexically_normal("/../../../", "/");

    test_lexically_normal("/../../../a/b/c", "/a/b/c");

    test_lexically_normal("a/..", ".");
    test_lexically_normal("a/../", ".");

#if defined(_WIN32)
    test_lexically_normal(R"(X:)", R"(X:)");

    test_lexically_normal(R"(X:DriveRelative)", R"(X:DriveRelative)");

    test_lexically_normal(R"(X:\)", R"(X:\)");
    test_lexically_normal(R"(X:/)", R"(X:\)");
    test_lexically_normal(R"(X:\\\)", R"(X:\)");
    test_lexically_normal(R"(X:///)", R"(X:\)");

    test_lexically_normal(R"(X:\DosAbsolute)", R"(X:\DosAbsolute)");
    test_lexically_normal(R"(X:/DosAbsolute)", R"(X:\DosAbsolute)");
    test_lexically_normal(R"(X:\\\DosAbsolute)", R"(X:\DosAbsolute)");
    test_lexically_normal(R"(X:///DosAbsolute)", R"(X:\DosAbsolute)");

    test_lexically_normal(R"(\RootRelative)", R"(\RootRelative)");
    test_lexically_normal(R"(/RootRelative)", R"(\RootRelative)");
    test_lexically_normal(R"(\\\RootRelative)", R"(\RootRelative)");
    test_lexically_normal(R"(///RootRelative)", R"(\RootRelative)");

    test_lexically_normal(R"(\\server\share)", R"(\\server\share)");
    test_lexically_normal(R"(//server/share)", R"(\\server\share)");
    test_lexically_normal(R"(\\server\\\share)", R"(\\server\share)");
    test_lexically_normal(R"(//server///share)", R"(\\server\share)");

    test_lexically_normal(R"(\\?\device)", R"(\\?\device)");
    test_lexically_normal(R"(//?/device)", R"(\\?\device)");

    test_lexically_normal(R"(\??\device)", R"(\??\device)");
    test_lexically_normal(R"(/??/device)", R"(\??\device)");

    test_lexically_normal(R"(\\.\device)", R"(\\.\device)");
    test_lexically_normal(R"(//./device)", R"(\\.\device)");

    test_lexically_normal(R"(\\?\UNC\server\share)", R"(\\?\UNC\server\share)");
    test_lexically_normal(R"(//?/UNC/server/share)", R"(\\?\UNC\server\share)");

    test_lexically_normal(R"(C:\a/b\\c\/d/\e//f)", R"(C:\a\b\c\d\e\f)");

    test_lexically_normal(R"(C:\meow\)", R"(C:\meow\)");
    test_lexically_normal(R"(C:\meow/)", R"(C:\meow\)");
    test_lexically_normal(R"(C:\meow\\)", R"(C:\meow\)");
    test_lexically_normal(R"(C:\meow\/)", R"(C:\meow\)");
    test_lexically_normal(R"(C:\meow/\)", R"(C:\meow\)");
    test_lexically_normal(R"(C:\meow//)", R"(C:\meow\)");

    test_lexically_normal(R"(C:\a\.\b\.\.\c\.\.\.)", R"(C:\a\b\c\)");
    test_lexically_normal(R"(C:\a\.\b\.\.\c\.\.\.\)", R"(C:\a\b\c\)");

    test_lexically_normal(R"(C:\a\b\c\d\e\..\f\..\..\..\g\h)", R"(C:\a\b\g\h)");

    test_lexically_normal(R"(C:\a\b\c\d\e\..\f\..\..\..\g\h\..)", R"(C:\a\b\g\)");
    test_lexically_normal(R"(C:\a\b\c\d\e\..\f\..\..\..\g\h\..\)", R"(C:\a\b\g\)");
    test_lexically_normal(
        R"(/\server/\share/\a/\b/\c/\./\./\d/\../\../\../\../\../\../\../\other/x/y/z/.././..\meow.txt)",
        R"(\\server\other\x\meow.txt)");
#endif
}

static void test_parent_path(Path input, StringView expected)
{
    const auto actual = input.parent_path();
    CHECK(actual == expected);
    bool parent_removes = actual != input.native();
    CHECK(input.make_parent_path() == parent_removes);
    CHECK(input.native() == expected);
}

TEST_CASE ("Path::make_parent_path and Path::parent_path", "[filesystem][files]")
{
    test_parent_path({}, {});
    test_parent_path("/a/", "/a");
    test_parent_path("/a/b", "/a");
    test_parent_path("/a////////b", "/a");
    test_parent_path("/a", "/");
    test_parent_path("/", "/");

#if defined(_WIN32)
    test_parent_path("C:/", "C:/");
    test_parent_path("C:/a", "C:/");
    test_parent_path("C:/a/", "C:/a");
    test_parent_path("C:/a/b", "C:/a");
    test_parent_path("C:", "C:");
    test_parent_path("C:a", "C:");
    test_parent_path("C:a/", "C:a");
    test_parent_path("C:a/b", "C:a");
    test_parent_path(R"(C:\)", R"(C:\)");
    test_parent_path(R"(C:\a)", R"(C:\)");
    test_parent_path(R"(C:\a\)", R"(C:\a)");
    test_parent_path(R"(C:\a\b)", R"(C:\a)");
    test_parent_path(R"(\\server\)", R"(\\server\)");
    test_parent_path(R"(\\server\a)", R"(\\server\)");
    test_parent_path(R"(\\server\a\)", R"(\\server\a)");
    test_parent_path(R"(\\server\a\b)", R"(\\server\a)");
#else  // ^^^ _WIN32 / !_WIN32 vvv
    test_parent_path("C:/", "C:");
    test_parent_path("C:/a", "C:");
    test_parent_path("C:/a/", "C:/a");
    test_parent_path("C:/a/b", "C:/a");
    test_parent_path("C:", "");
    test_parent_path("C:a", "");
    test_parent_path("C:a/", "C:a");
    test_parent_path("C:a/b", "C:a");
    test_parent_path(R"(C:\)", "");
    test_parent_path(R"(C:\a)", "");
    test_parent_path(R"(C:\a\)", "");
    test_parent_path(R"(C:\a\b)", "");
    test_parent_path(R"(\\server\)", "");
    test_parent_path(R"(\\server\a)", "");
    test_parent_path(R"(\\server\a\)", "");
    test_parent_path(R"(\\server\a\b)", "");
#endif // ^^^ !_WIN32
}

static void test_path_decomposition(
    Path input, bool is_absolute, StringView expected_stem, StringView expected_extension, StringView ads = {})
{
    auto expected_filename = expected_stem.to_string();
    expected_filename.append(expected_extension.data(), expected_extension.size());
    expected_filename.append(ads.data(), ads.size());
    CHECK(input.is_absolute() == is_absolute);
    CHECK(input.is_relative() != is_absolute);
    CHECK(input.filename() == expected_filename);
    CHECK(input.stem() == expected_stem);
    CHECK(input.extension() == expected_extension);
}

TEST_CASE ("Path decomposition", "[filesystem][files]")
{
    test_path_decomposition("", false, "", "");
    test_path_decomposition("a/b", false, "b", "");
    test_path_decomposition("a/b", false, "b", "");
    test_path_decomposition("a/b.ext", false, "b", ".ext");
    test_path_decomposition("a/b.ext.ext", false, "b.ext", ".ext");
    test_path_decomposition("a/.config", false, ".config", "");
    test_path_decomposition("a/..config", false, ".", ".config");
#if defined(_WIN32)
    test_path_decomposition(
        "a/hello.world.config:alternate-data-stream", false, "hello.world", ".config", ":alternate-data-stream");
    test_path_decomposition("a/.config:alternate-data-stream", false, ".config", "", ":alternate-data-stream");
#endif // _WIN32

    bool single_slash_is_absolute =
#if defined(_WIN32)
        false
#else  // ^^^ _WIN32 // !_WIN32 vvv
        true
#endif // ^^^ !_WIN32
        ;

    bool drive_is_absolute =
#if defined(_WIN32)
        true
#else  // ^^^ _WIN32 // !_WIN32 vvv
        false
#endif // ^^^ !_WIN32
        ;
    test_path_decomposition("/a/b", single_slash_is_absolute, "b", "");
    test_path_decomposition("/a/b.ext", single_slash_is_absolute, "b", ".ext");

#if defined(_WIN32)
    test_path_decomposition("C:a", false, "a", "");
    test_path_decomposition("C:a.ext", false, "a", ".ext");
#else  // ^^^ _WIN32 // !_WIN32 vvv
    test_path_decomposition("C:a", false, "C:a", "");
    test_path_decomposition("C:a.ext", false, "C:a", ".ext");
#endif // ^^^ !_WIN32

    test_path_decomposition("C:/a", drive_is_absolute, "a", "");
    test_path_decomposition("C:/a.ext", drive_is_absolute, "a", ".ext");
    test_path_decomposition("//server/a", true, "a", "");
    test_path_decomposition("//server/a.ext", true, "a", ".ext");
}

TEST_CASE ("remove all", "[files]")
{
    urbg_t urbg;

    auto& fs = setup();

    auto temp_dir = base_temporary_directory() / get_random_filename(urbg);
    INFO("temp dir is: " << temp_dir.native());

    create_directory_tree(urbg, fs, temp_dir);

    std::error_code ec;
    Path fp;
    fs.remove_all(temp_dir, ec, fp);
    CHECK_EC_ON_FILE(fp, ec);

    REQUIRE_FALSE(fs.exists(temp_dir, ec));
    CHECK_EC_ON_FILE(temp_dir, ec);
}

TEST_CASE ("remove all symlinks", "[files]")
{
    urbg_t urbg;

    auto& fs = setup();

    auto temp_dir = base_temporary_directory() / get_random_filename(urbg);
    INFO("temp dir is: " << temp_dir.native());

    const auto target_root = temp_dir / "target";
    fs.create_directories(target_root, VCPKG_LINE_INFO);
    const auto target_file = target_root / "file.txt";
    fs.write_contents(target_file, "", VCPKG_LINE_INFO);
    const auto symlink_inside_dir = temp_dir / "symlink_inside";
    fs.create_directory(symlink_inside_dir, VCPKG_LINE_INFO);
    std::error_code ec;
    fs.create_directory_symlink(target_root, symlink_inside_dir / "symlink", ec);
    if (ec)
    {
        // if we get not supported or permission denied, assume symlinks aren't supported
        // on this system and the test is a no-op
        REQUIRE((ec == std::errc::not_supported || ec == std::errc::permission_denied));
    }
    else
    {
        const auto symlink_direct = temp_dir / "direct_symlink";
        fs.create_directory_symlink(target_root, symlink_direct, VCPKG_LINE_INFO);

        // removing a directory with a symlink inside should remove the symlink and not the target:
        fs.remove_all(symlink_inside_dir, VCPKG_LINE_INFO);
        REQUIRE(!fs.exists(symlink_inside_dir, VCPKG_LINE_INFO));
        REQUIRE(fs.exists(target_root, VCPKG_LINE_INFO));

        // removing a symlink should remove the symlink and not the target:
        fs.remove_all(symlink_direct, VCPKG_LINE_INFO);
        REQUIRE(!fs.exists(symlink_direct, VCPKG_LINE_INFO));
        REQUIRE(fs.exists(target_root, VCPKG_LINE_INFO));
    }

    Path fp;
    fs.remove_all(temp_dir, ec, fp);
    CHECK_EC_ON_FILE(fp, ec);

    REQUIRE_FALSE(fs.exists(temp_dir, ec));
    CHECK_EC_ON_FILE(temp_dir, ec);
}

TEST_CASE ("get_files_recursive_symlinks", "[files]")
{
    do_filesystem_enumeration_test(
        [](Filesystem& fs, const Path& root) { return fs.get_files_recursive(root, VCPKG_LINE_INFO); },
        [](const Path& root) {
            return std::vector<Path>{
                root / "file.txt",
                root / "some-directory",
                root / "some-directory" / "file2.txt",
                root / "some-directory" / "some-inner-directory",
                root / "some-directory" / "symlink-to-file2.txt",
                root / "some-directory" / "symlink-to-some-inner-directory",
                root / "symlink-to-file.txt",
                root / "symlink-to-some-directory",
            };
        });
}

TEST_CASE ("get_files_non_recursive_symlinks", "[files]")
{
    do_filesystem_enumeration_test(
        [](Filesystem& fs, const Path& root) { return fs.get_files_non_recursive(root, VCPKG_LINE_INFO); },
        [](const Path& root) {
            return std::vector<Path>{
                root / "file.txt",
                root / "some-directory",
                root / "symlink-to-file.txt",
                root / "symlink-to-some-directory",
            };
        });
}

TEST_CASE ("get_directories_recursive_symlinks", "[files]")
{
    do_filesystem_enumeration_test(
        [](Filesystem& fs, const Path& root) { return fs.get_directories_recursive(root, VCPKG_LINE_INFO); },
        [](const Path& root) {
            return std::vector<Path>{
                root / "some-directory",
                root / "some-directory" / "some-inner-directory",
                root / "some-directory" / "symlink-to-some-inner-directory",
                root / "symlink-to-some-directory",
            };
        });
}

TEST_CASE ("get_directories_non_recursive_symlinks", "[files]")
{
    do_filesystem_enumeration_test(
        [](Filesystem& fs, const Path& root) { return fs.get_directories_non_recursive(root, VCPKG_LINE_INFO); },
        [](const Path& root) {
            return std::vector<Path>{
                root / "some-directory",
                root / "symlink-to-some-directory",
            };
        });
}

TEST_CASE ("get_regular_files_recursive_symlinks", "[files]")
{
    do_filesystem_enumeration_test(
        [](Filesystem& fs, const Path& root) { return fs.get_regular_files_recursive(root, VCPKG_LINE_INFO); },
        [](const Path& root) {
            return std::vector<Path>{
                root / "file.txt",
                root / "some-directory" / "file2.txt",
                root / "some-directory" / "symlink-to-file2.txt",
                root / "symlink-to-file.txt",
            };
        });
}

TEST_CASE ("get_regular_files_non_recursive_symlinks", "[files]")
{
    do_filesystem_enumeration_test(
        [](Filesystem& fs, const Path& root) { return fs.get_regular_files_non_recursive(root, VCPKG_LINE_INFO); },
        [](const Path& root) {
            return std::vector<Path>{
                root / "file.txt",
                root / "symlink-to-file.txt",
            };
        });
}

TEST_CASE ("copy_symlink", "[files]")
{
    urbg_t urbg;

    auto& fs = setup();

    auto temp_dir = base_temporary_directory() / get_random_filename(urbg);
    INFO("temp dir is: " << temp_dir.native());

    fs.create_directory(temp_dir, VCPKG_LINE_INFO);
    fs.create_directory(temp_dir / "dir", VCPKG_LINE_INFO);
    fs.write_contents(temp_dir / "file", "some file contents", VCPKG_LINE_INFO);

    std::error_code ec;
    fs.create_symlink("../file", temp_dir / "dir/sym", ec); // note: relative
    if (ec)
    {
        // if we get not supported or permission denied, assume symlinks aren't supported
        // on this system and the test is a no-op
        REQUIRE((ec == std::errc::not_supported || ec == std::errc::permission_denied));
    }
    else
    {
        REQUIRE(fs.read_contents(temp_dir / "dir/sym", VCPKG_LINE_INFO) == "some file contents");
        fs.copy_symlink(temp_dir / "dir/sym", temp_dir / "dir/sym_copy", VCPKG_LINE_INFO);
        REQUIRE(fs.read_contents(temp_dir / "dir/sym_copy", VCPKG_LINE_INFO) == "some file contents");
    }

    Path fp;
    fs.remove_all(temp_dir, ec, fp);
    CHECK_EC_ON_FILE(fp, ec);

    REQUIRE_FALSE(fs.exists(temp_dir, ec));
    CHECK_EC_ON_FILE(temp_dir, ec);
}

TEST_CASE ("LinesCollector", "[files]")
{
    using Strings::LinesCollector;
    LinesCollector lc;
    CHECK(lc.extract() == std::vector<std::string>{""});
    lc.on_data({"a\nb\r\nc\rd\r\r\n\ne\n\rx", 16});
    CHECK(lc.extract() == std::vector<std::string>{"a", "b", "c", "d", "", "", "e", "", "x"});
    CHECK(lc.extract() == std::vector<std::string>{""});
    lc.on_data({"hello ", 6});
    lc.on_data({"there ", 6});
    lc.on_data({"world", 5});
    CHECK(lc.extract() == std::vector<std::string>{"hello there world"});
    lc.on_data({"\r\nhello \r\n", 10});
    lc.on_data({"\r\nworld", 7});
    CHECK(lc.extract() == std::vector<std::string>{"", "hello ", "", "world"});
    lc.on_data({"\r\n\r\n\r\n", 6});
    CHECK(lc.extract() == std::vector<std::string>{"", "", "", ""});
    lc.on_data({"a", 1});
    lc.on_data({"b\nc", 3});
    lc.on_data({"d", 1});
    CHECK(lc.extract() == std::vector<std::string>{"ab", "cd"});
    lc.on_data({"a\r", 2});
    lc.on_data({"\nb", 2});
    CHECK(lc.extract() == std::vector<std::string>{"a", "b"});
    lc.on_data({"a\r", 2});
    CHECK(lc.extract() == std::vector<std::string>{"a", ""});
    lc.on_data({"\n", 1});
    CHECK(lc.extract() == std::vector<std::string>{"", ""});
    lc.on_data({"\rabc\n", 5});
    CHECK(lc.extract() == std::vector<std::string>{"", "abc", ""});
}

#if defined(_WIN32)
TEST_CASE ("win32_fix_path_case", "[files]")
{
    // This test assumes that the Windows directory is C:\Windows

    CHECK(win32_fix_path_case("") == "");

    CHECK(win32_fix_path_case("C:") == "C:");
    CHECK(win32_fix_path_case("c:") == "C:");
    CHECK(win32_fix_path_case("C:/") == "C:\\");
    CHECK(win32_fix_path_case("C:\\") == "C:\\");
    CHECK(win32_fix_path_case("c:\\") == "C:\\");
    CHECK(win32_fix_path_case("C:\\WiNdOws") == "C:\\Windows");
    CHECK(win32_fix_path_case("c:\\WiNdOws\\") == "C:\\Windows\\");
    CHECK(win32_fix_path_case("C://///////WiNdOws") == "C:\\Windows");
    CHECK(win32_fix_path_case("c:\\/\\/WiNdOws\\/") == "C:\\Windows\\");

    auto& fs = get_real_filesystem();
    auto original_cwd = fs.current_path(VCPKG_LINE_INFO);
    fs.current_path("C:\\", VCPKG_LINE_INFO);
    CHECK(win32_fix_path_case("\\") == "\\");
    CHECK(win32_fix_path_case("\\/\\WiNdOws") == "\\Windows");
    CHECK(win32_fix_path_case("\\WiNdOws") == "\\Windows");
    CHECK(win32_fix_path_case("\\WiNdOws") == "\\Windows");
    CHECK(win32_fix_path_case("c:WiNdOws") == "C:Windows");
    CHECK(win32_fix_path_case("c:WiNdOws/system32") == "C:Windows\\System32");
    fs.current_path(original_cwd, VCPKG_LINE_INFO);

    fs.create_directories("SuB/Dir/Ectory", VCPKG_LINE_INFO);
    CHECK(win32_fix_path_case("sub") == "SuB");
    CHECK(win32_fix_path_case("SUB") == "SuB");
    CHECK(win32_fix_path_case("sub/") == "SuB\\");
    CHECK(win32_fix_path_case("sub/dir") == "SuB\\Dir");
    CHECK(win32_fix_path_case("sub/dir/") == "SuB\\Dir\\");
    CHECK(win32_fix_path_case("sub/dir/ectory") == "SuB\\Dir\\Ectory");
    CHECK(win32_fix_path_case("sub/dir/ectory/") == "SuB\\Dir\\Ectory\\");
    fs.remove_all("SuB", VCPKG_LINE_INFO);

    CHECK(win32_fix_path_case("//nonexistent_server\\nonexistent_share\\") ==
          "\\\\nonexistent_server\\nonexistent_share\\");
    CHECK(win32_fix_path_case("\\\\nonexistent_server\\nonexistent_share\\") ==
          "\\\\nonexistent_server\\nonexistent_share\\");
    CHECK(win32_fix_path_case("\\\\nonexistent_server\\nonexistent_share") ==
          "\\\\nonexistent_server\\nonexistent_share");

    CHECK(win32_fix_path_case("///three_slashes_not_a_server\\subdir\\") == "\\three_slashes_not_a_server\\subdir\\");

    CHECK(win32_fix_path_case("\\??\\c:\\WiNdOws") == "\\??\\c:\\WiNdOws");
    CHECK(win32_fix_path_case("\\\\?\\c:\\WiNdOws") == "\\\\?\\c:\\WiNdOws");
    CHECK(win32_fix_path_case("\\\\.\\c:\\WiNdOws") == "\\\\.\\c:\\WiNdOws");
    CHECK(win32_fix_path_case("c:\\/\\/Nonexistent\\/path/here") == "C:\\Nonexistent\\path\\here");
}
#endif // _WIN32

#if defined(CATCH_CONFIG_ENABLE_BENCHMARKING)
TEST_CASE ("remove all -- benchmarks", "[files][!benchmark]")
{
    urbg_t urbg;
    auto& fs = setup();

    struct
    {
        urbg_t& urbg;
        Filesystem& fs;

        void operator()(Catch::Benchmark::Chronometer& meter, std::uint32_t max_depth) const
        {
            std::vector<path> temp_dirs;
            temp_dirs.resize(meter.runs());

            std::generate(begin(temp_dirs), end(temp_dirs), [&] {
                path temp_dir = base_temporary_directory() / get_random_filename(urbg);
                create_directory_tree(urbg, fs, temp_dir, max_depth);
                return temp_dir;
            });

            meter.measure([&](int run) {
                std::error_code ec;
                path fp;
                const auto& temp_dir = temp_dirs[run];

                fs.remove_all(temp_dir, ec, fp);
                CHECK_EC_ON_FILE(fp, ec);
            });

            for (const auto& dir : temp_dirs)
            {
                std::error_code ec;
                REQUIRE_FALSE(fs.exists(dir, ec));
                CHECK_EC_ON_FILE(dir, ec);
            }
        }
    } do_benchmark = {urbg, fs};

    BENCHMARK_ADVANCED("small directory")(Catch::Benchmark::Chronometer meter) { do_benchmark(meter, 2); };

    BENCHMARK_ADVANCED("large directory")(Catch::Benchmark::Chronometer meter) { do_benchmark(meter, 5); };
}
#endif
