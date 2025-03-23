#include <vcpkg/base/system-headers.h>

#include <vcpkg-test/util.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/strings.h>

#include <iostream>
#include <random>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif // ^^^ !_WIN32

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

    std::string get_random_filename(urbg_t& urbg, StringLiteral tag)
    {
        return Strings::b32_encode(urbg()).append(tag.data(), tag.size());
    }

    bool is_valid_symlink_failure(const std::error_code& ec) noexcept
    {
#if defined(__MINGW32__)
        // mingw doesn't support symlink operations
        return true;
#elif defined(_WIN32)
        // on Windows, creating symlinks requires admin rights, so we ignore such failures
        return ec == std::error_code(ERROR_PRIVILEGE_NOT_HELD, std::system_category());
#else  // ^^^ _WIN32 // !_WIN32
        (void)ec;
        return false; // symlinks should always work on non-windows
#endif // ^^^ !_WIN32
    }

    void create_directory_tree(urbg_t& urbg, const Filesystem& fs, const Path& base, std::uint32_t remaining_depth = 5)
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
            for (unsigned int i = 0; i < 5; ++i)
            {
                create_directory_tree(urbg, fs, base / get_random_filename(urbg, "_tree"), remaining_depth - 1);
            }

#if !defined(_WIN32)
            if (urbg() & 1u)
            {
                const auto chmod_result = ::chmod(base.c_str(), 0444);
                if (chmod_result != 0)
                {
                    const auto failure_message = std::generic_category().message(errno);
                    FAIL("chmod failed with " << failure_message);
                }
            }
            if (urbg() & 2u)
            {
                const auto chmod_result = ::chmod(base.c_str(), 0000); // e.g. bazel sandbox
                if (chmod_result != 0)
                {
                    const auto failure_message = std::generic_category().message(errno);
                    FAIL("chmod failed with " << failure_message);
                }
            }
#endif // ^^^ !_WIN32
        }

        REQUIRE(exists(fs.symlink_status(base, ec)));
        CHECK_EC_ON_FILE(base, ec);
    }

    const Filesystem& setup()
    {
        std::error_code ec;
        real_filesystem.create_directory(base_temporary_directory(), ec);
        CHECK_EC_ON_FILE(base_temporary_directory(), ec);

        return real_filesystem;
    }

    template<class Enumerator, class ExpectedGenerator>
    void do_filesystem_enumeration_test(Enumerator&& enumerator, ExpectedGenerator&& generate_expected)
    {
        // Note: not seeded with random data, so this will always produce the same sequence of names
        urbg_t urbg;

        auto& fs = setup();

        auto temp_dir = base_temporary_directory() / get_random_filename(urbg, "_enum");
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

        fs.remove_all(temp_dir, VCPKG_LINE_INFO);

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
            INFO(ec.message());
            REQUIRE(is_valid_symlink_failure(ec));
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

static void set_readonly(const Path& target)
{
#if defined(_WIN32)
    auto as_unicode = Strings::to_utf16(target.native());

    const DWORD old_attributes = ::GetFileAttributesW(as_unicode.c_str());
    if (old_attributes == INVALID_FILE_ATTRIBUTES)
    {
        throw std::runtime_error("failed to get existing attributes to set readonly");
    }

    const DWORD new_attributes = old_attributes | FILE_ATTRIBUTE_READONLY;
    if (::SetFileAttributesW(as_unicode.c_str(), new_attributes) == 0)
    {
        throw std::runtime_error("failed to set readonly attributes");
    }
#else  // ^^^ _WIN32 // !_WIN32 vvv
    struct stat s;
    if (::stat(target.c_str(), &s) != 0)
    {
        throw std::runtime_error("failed to get existing attributes to set readonly");
    }

    const mode_t all_write_bits = 0222;
    const mode_t all_except_write_bits = ~all_write_bits;
    const mode_t new_bits = s.st_mode & all_except_write_bits;
    if (::chmod(target.c_str(), new_bits) != 0)
    {
        throw std::runtime_error("failed to set readonly attributes");
    }
#endif // ^^^ !_WIN32
}

TEST_CASE ("remove readonly", "[files]")
{
    urbg_t urbg;

    auto& fs = setup();

    auto temp_dir = base_temporary_directory() / get_random_filename(urbg, "_remove_readonly");
    INFO("temp dir is: " << temp_dir.native());

    fs.create_directory(temp_dir, VCPKG_LINE_INFO);
    const auto writable_dir = temp_dir / "writable_dir";
    fs.create_directory(writable_dir, VCPKG_LINE_INFO);

    const auto writable_dir_writable_file = writable_dir / "writable_file";
    fs.write_contents(writable_dir_writable_file, "content", VCPKG_LINE_INFO);

    const auto writable_dir_readonly_file = writable_dir / "readonly_file";
    fs.write_contents(writable_dir_readonly_file, "content", VCPKG_LINE_INFO);
    set_readonly(writable_dir_readonly_file);

    CHECK(fs.remove(writable_dir_writable_file, VCPKG_LINE_INFO));
    CHECK(fs.remove(writable_dir_readonly_file, VCPKG_LINE_INFO));

    CHECK(fs.remove(writable_dir, VCPKG_LINE_INFO));

#if defined(_WIN32)
    // On Win32, FILE_ATTRIBUTE_READONLY on directories should be ignored by remove.
    // We don't support resolving this problem on POSIX because in all the places where it
    // would matter, vcpkg doesn't create directories without writable bits (for now).
    const auto readonly_dir = temp_dir / "readonly_dir";
    fs.create_directory(readonly_dir, VCPKG_LINE_INFO);

    const auto readonly_dir_writable_file = readonly_dir / "writable_file";
    fs.write_contents(readonly_dir_writable_file, "content", VCPKG_LINE_INFO);

    const auto readonly_dir_readonly_file = readonly_dir / "readonly_file";
    fs.write_contents(readonly_dir_readonly_file, "content", VCPKG_LINE_INFO);
    set_readonly(readonly_dir_readonly_file);

    set_readonly(readonly_dir);

    CHECK(fs.remove(readonly_dir_writable_file, VCPKG_LINE_INFO));
    CHECK(fs.remove(readonly_dir_readonly_file, VCPKG_LINE_INFO));

    CHECK(fs.remove(readonly_dir, VCPKG_LINE_INFO));
#endif // ^^^ _WIN32

    CHECK(fs.remove(temp_dir, VCPKG_LINE_INFO));
    std::error_code ec;
    REQUIRE_FALSE(fs.exists(temp_dir, ec));
    CHECK_EC_ON_FILE(temp_dir, ec);
}

TEST_CASE ("remove all", "[files]")
{
    urbg_t urbg;

    auto& fs = setup();

    auto temp_dir = base_temporary_directory() / get_random_filename(urbg, "_remove_all");
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

    auto temp_dir = base_temporary_directory() / get_random_filename(urbg, "_remove_all_symlinks");
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
        REQUIRE(is_valid_symlink_failure(ec));
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
        [](const Filesystem& fs, const Path& root) { return fs.get_files_recursive(root, VCPKG_LINE_INFO); },
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

TEST_CASE ("get_regular_files_recursive_proximate_symlinks", "[files]")
{
    do_filesystem_enumeration_test(
        [](const Filesystem& fs, const Path& root) {
            return fs.get_regular_files_recursive_lexically_proximate(root, VCPKG_LINE_INFO);
        },
        [](const Path&) {
            Path somedir{"some-directory"};
            return std::vector<Path>{
                "file.txt",
                somedir / "file2.txt",
                somedir / "symlink-to-file2.txt",
                "symlink-to-file.txt",
            };
        });
}

TEST_CASE ("get_files_non_recursive_symlinks", "[files]")
{
    do_filesystem_enumeration_test(
        [](const Filesystem& fs, const Path& root) { return fs.get_files_non_recursive(root, VCPKG_LINE_INFO); },
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
        [](const Filesystem& fs, const Path& root) { return fs.get_directories_recursive(root, VCPKG_LINE_INFO); },
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
        [](const Filesystem& fs, const Path& root) { return fs.get_directories_non_recursive(root, VCPKG_LINE_INFO); },
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
        [](const Filesystem& fs, const Path& root) { return fs.get_regular_files_recursive(root, VCPKG_LINE_INFO); },
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
        [](const Filesystem& fs, const Path& root) {
            return fs.get_regular_files_non_recursive(root, VCPKG_LINE_INFO);
        },
        [](const Path& root) {
            return std::vector<Path>{
                root / "file.txt",
                root / "symlink-to-file.txt",
            };
        });
}

TEST_CASE ("copy_file", "[files]")
{
    urbg_t urbg;

    auto& fs = setup();

    auto temp_dir = base_temporary_directory() / get_random_filename(urbg, "_copy_file");
    INFO("temp dir is: " << temp_dir.native());

    fs.create_directory(temp_dir, VCPKG_LINE_INFO);
    const auto existing_from = temp_dir / "a";
    constexpr StringLiteral existing_from_contents = "hello there";
    fs.write_contents(existing_from, existing_from_contents, VCPKG_LINE_INFO);

    const auto existing_to = temp_dir / "already_existing";
    constexpr StringLiteral existing_to_contents = "already existing file";
    fs.write_contents(existing_to, existing_to_contents, VCPKG_LINE_INFO);

    std::error_code ec;

    // N4861 [fs.op.copy.file]/4.1:
    // "report an error [...] if ..."
    //
    // is_regular_file(from) is false
    REQUIRE(!fs.copy_file(temp_dir, temp_dir / "b", CopyOptions::overwrite_existing, ec));
    REQUIRE(ec);
    REQUIRE(!fs.copy_file(temp_dir / "nonexistent", temp_dir / "b", CopyOptions::overwrite_existing, ec));
    REQUIRE(ec);

    // exists(to) is true and is_regular_file(to) is false
    fs.create_directory(temp_dir / "a_directory", VCPKG_LINE_INFO);
    REQUIRE(!fs.copy_file(existing_from, temp_dir / "a_directory", CopyOptions::overwrite_existing, ec));
    REQUIRE(ec);

    // exists(to) is true and equivalent(from, true) is true
    REQUIRE(!fs.copy_file(existing_from, temp_dir / "a/../a", CopyOptions::overwrite_existing, ec));
    REQUIRE(ec);

    // exists(to) is true and [neither skip_existing nor overwrite_existing]
    REQUIRE(!fs.copy_file(existing_from, existing_to, CopyOptions::none, ec));
    REQUIRE(ec);

    // Otherwise, copy the contents and attributes of the file from resolves to to the file
    // to resolves to, if

    // exists(to) is false
    REQUIRE(fs.copy_file(existing_from, temp_dir / "b", CopyOptions::none, ec));
    REQUIRE(!ec);
    REQUIRE(fs.read_contents(temp_dir / "b", VCPKG_LINE_INFO) == existing_from_contents);

    // [skip_existing]
    REQUIRE(!fs.copy_file(existing_from, existing_to, CopyOptions::skip_existing, ec));
    REQUIRE(!ec);
    REQUIRE(fs.read_contents(existing_to, VCPKG_LINE_INFO) == existing_to_contents);

    // [overwrite_existing]
    REQUIRE(fs.copy_file(existing_from, existing_to, CopyOptions::overwrite_existing, ec));
    REQUIRE(!ec);
    REQUIRE(fs.read_contents(existing_to, VCPKG_LINE_INFO) == existing_from_contents);

#if !defined(_WIN32)
    // Also check that mode bits are copied
    REQUIRE(::chmod(existing_from.c_str(), 0555) == 0); // note: not writable
    const auto attributes_target = temp_dir / "attributes_target";
    fs.copy_file(existing_from, attributes_target, CopyOptions::none, VCPKG_LINE_INFO);
    REQUIRE(fs.read_contents(attributes_target, VCPKG_LINE_INFO) == existing_from_contents);
    struct stat copied_attributes_stat;
    REQUIRE(::stat(attributes_target.c_str(), &copied_attributes_stat) == 0);
    const auto actual_mode = copied_attributes_stat.st_mode & 0777;
    REQUIRE(actual_mode == 0555);
#endif // ^^^ !_WIN32

    Path fp;
    fs.remove_all(temp_dir, ec, fp);
    CHECK_EC_ON_FILE(fp, ec);

    REQUIRE_FALSE(fs.exists(temp_dir, ec));
    CHECK_EC_ON_FILE(temp_dir, ec);
}

TEST_CASE ("rename", "[files]")
{
    urbg_t urbg;

    auto& fs = setup();

    auto temp_dir = base_temporary_directory() / get_random_filename(urbg, "_rename");
    INFO("temp dir is: " << temp_dir.native());

    static constexpr StringLiteral FileTxt = "file.txt";
    fs.remove_all(temp_dir, VCPKG_LINE_INFO);
    fs.create_directory(temp_dir, VCPKG_LINE_INFO);
    auto temp_dir_a = temp_dir / "a";
    fs.create_directory(temp_dir_a, VCPKG_LINE_INFO);
    auto temp_dir_a_file = temp_dir_a / FileTxt;
    auto temp_dir_b = temp_dir / "b";
    auto temp_dir_b_file = temp_dir_b / FileTxt;

    static constexpr StringLiteral text_file_contents = "hello there";
    fs.write_contents(temp_dir_a_file, text_file_contents, VCPKG_LINE_INFO);

    // try rename_with_retry
    {
        fs.rename_with_retry(temp_dir_a, temp_dir_b, VCPKG_LINE_INFO);
        REQUIRE(!fs.exists(temp_dir_a, VCPKG_LINE_INFO));
        REQUIRE(fs.read_contents(temp_dir_b_file, VCPKG_LINE_INFO) == text_file_contents);

        // put things back
        fs.rename(temp_dir_b, temp_dir_a, VCPKG_LINE_INFO);
        REQUIRE(fs.read_contents(temp_dir_a_file, VCPKG_LINE_INFO) == text_file_contents);
        REQUIRE(!fs.exists(temp_dir_b, VCPKG_LINE_INFO));
    }

    // try rename_or_delete directory, target does not exist
    {
        REQUIRE(fs.rename_or_delete(temp_dir_a, temp_dir_b, VCPKG_LINE_INFO));
        REQUIRE(!fs.exists(temp_dir_a, VCPKG_LINE_INFO));
        REQUIRE(fs.read_contents(temp_dir_b_file, VCPKG_LINE_INFO) == text_file_contents);

        // put things back
        fs.rename(temp_dir_b, temp_dir_a, VCPKG_LINE_INFO);
        REQUIRE(fs.read_contents(temp_dir_a_file, VCPKG_LINE_INFO) == text_file_contents);
        REQUIRE(!fs.exists(temp_dir_b, VCPKG_LINE_INFO));
    }

    // try rename_or_delete directory, target exists
    {
        fs.create_directory(temp_dir_b, VCPKG_LINE_INFO);
        fs.write_contents(temp_dir_b_file, text_file_contents, VCPKG_LINE_INFO);

        // Note that the VCPKG_LINE_INFO overload implicitly tests that ec got cleared
        REQUIRE(!fs.rename_or_delete(temp_dir_a, temp_dir_b, VCPKG_LINE_INFO));
        REQUIRE(!fs.exists(temp_dir_a, VCPKG_LINE_INFO));
        REQUIRE(fs.read_contents(temp_dir_b_file, VCPKG_LINE_INFO) == text_file_contents);

        // put things back
        fs.rename(temp_dir_b, temp_dir_a, VCPKG_LINE_INFO);
        REQUIRE(fs.read_contents(temp_dir_a_file, VCPKG_LINE_INFO) == text_file_contents);
        REQUIRE(!fs.exists(temp_dir_b, VCPKG_LINE_INFO));
    }

    // try rename_or_delete file, target does not exist
    {
        fs.create_directory(temp_dir_b, VCPKG_LINE_INFO);
        REQUIRE(fs.rename_or_delete(temp_dir_a_file, temp_dir_b_file, VCPKG_LINE_INFO));
        REQUIRE(!fs.exists(temp_dir_a_file, VCPKG_LINE_INFO));
        REQUIRE(fs.read_contents(temp_dir_b_file, VCPKG_LINE_INFO) == text_file_contents);

        // put things back
        fs.rename(temp_dir_b_file, temp_dir_a_file, VCPKG_LINE_INFO);
        REQUIRE(fs.read_contents(temp_dir_a_file, VCPKG_LINE_INFO) == text_file_contents);
        REQUIRE(!fs.exists(temp_dir_b_file, VCPKG_LINE_INFO));
        fs.remove(temp_dir_b, VCPKG_LINE_INFO);
    }

    // try rename_or_delete file, target exists
    {
        fs.create_directory(temp_dir_b, VCPKG_LINE_INFO);
        fs.write_contents(temp_dir_b_file, text_file_contents, VCPKG_LINE_INFO);
        // Note that the VCPKG_LINE_INFO overload implicitly tests that ec got cleared
        // Also note that POSIX rename() will just delete the target like we want by itself so
        // this returns true.
        REQUIRE(fs.rename_or_delete(temp_dir_a_file, temp_dir_b_file, VCPKG_LINE_INFO));
        REQUIRE(!fs.exists(temp_dir_a_file, VCPKG_LINE_INFO));
        REQUIRE(fs.read_contents(temp_dir_b_file, VCPKG_LINE_INFO) == text_file_contents);

        // put things back
        fs.rename(temp_dir_b_file, temp_dir_a_file, VCPKG_LINE_INFO);
        REQUIRE(fs.read_contents(temp_dir_a_file, VCPKG_LINE_INFO) == text_file_contents);
        REQUIRE(!fs.exists(temp_dir_b_file, VCPKG_LINE_INFO));
        fs.remove(temp_dir_b, VCPKG_LINE_INFO);
    }
}

TEST_CASE ("copy_symlink", "[files]")
{
    urbg_t urbg;

    auto& fs = setup();

    auto temp_dir = base_temporary_directory() / get_random_filename(urbg, "_copy_symlink");
    INFO("temp dir is: " << temp_dir.native());

    fs.create_directory(temp_dir, VCPKG_LINE_INFO);
    fs.create_directory(temp_dir / "dir", VCPKG_LINE_INFO);
    fs.write_contents(temp_dir / "file", "some file contents", VCPKG_LINE_INFO);

    std::error_code ec;
    fs.create_symlink("../file", temp_dir / "dir/sym", ec); // note: relative
    if (ec)
    {
        REQUIRE(is_valid_symlink_failure(ec));
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
    CHECK(lc.extract() == std::vector<std::string>{});
    lc.on_data({"a\nb\r\nc\rd\r\r\n\ne\n\rx", 16});
    CHECK(lc.extract() == std::vector<std::string>{"a", "b", "c", "d", "", "", "e", "", "x"});
    CHECK(lc.extract() == std::vector<std::string>{});
    lc.on_data({"hello ", 6});
    lc.on_data({"there ", 6});
    lc.on_data({"world", 5});
    CHECK(lc.extract() == std::vector<std::string>{"hello there world"});
    lc.on_data({"\r\nhello \r\n", 10});
    lc.on_data({"\r\nworld", 7});
    CHECK(lc.extract() == std::vector<std::string>{"", "hello ", "", "world"});
    lc.on_data({"\r\n\r\n\r\n", 6});
    CHECK(lc.extract() == std::vector<std::string>{"", "", ""});
    lc.on_data({"a", 1});
    lc.on_data({"b\nc", 3});
    lc.on_data({"d", 1});
    CHECK(lc.extract() == std::vector<std::string>{"ab", "cd"});
    lc.on_data({"a\r", 2});
    lc.on_data({"\nb", 2});
    CHECK(lc.extract() == std::vector<std::string>{"a", "b"});
    lc.on_data({"a\r", 2});
    CHECK(lc.extract() == std::vector<std::string>{"a"});
    lc.on_data({"\n", 1});
    CHECK(lc.extract() == std::vector<std::string>{""});
    lc.on_data({"\rabc\n", 5});
    CHECK(lc.extract() == std::vector<std::string>{"", "abc"});
}

TEST_CASE ("find_file_recursively_up", "[files]")
{
    auto& fs = setup();
    auto test_root = base_temporary_directory() / "find_file_recursively_up_test";
    fs.create_directory(test_root, VCPKG_LINE_INFO);
    auto one = test_root / "one";
    auto two = one / "two";
    fs.create_directory(one, VCPKG_LINE_INFO);
    fs.create_directory(two, VCPKG_LINE_INFO);
    auto one_marker = one / ".one-marker";
    fs.write_contents(one_marker, "", VCPKG_LINE_INFO);

    std::error_code ec;
    auto result = fs.find_file_recursively_up(test_root, ".one-marker", ec);
    REQUIRE(result.empty());
    REQUIRE(!ec);

    result = fs.find_file_recursively_up(one, ".one-marker", ec);
    REQUIRE(result == one);
    REQUIRE(!ec);
    result = fs.find_file_recursively_up(one_marker, ".one-marker", ec);
    REQUIRE(result == one);
    REQUIRE(!ec);
    result = fs.find_file_recursively_up(two, ".one-marker", ec);
    REQUIRE(result == one);
    REQUIRE(!ec);

    fs.remove_all(test_root, VCPKG_LINE_INFO);
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

    auto original_cwd = real_filesystem.current_path(VCPKG_LINE_INFO);
    real_filesystem.current_path("C:\\", VCPKG_LINE_INFO);
    CHECK(win32_fix_path_case("\\") == "\\");
    CHECK(win32_fix_path_case("\\/\\WiNdOws") == "\\Windows");
    CHECK(win32_fix_path_case("\\WiNdOws") == "\\Windows");
    CHECK(win32_fix_path_case("\\WiNdOws") == "\\Windows");
    CHECK(win32_fix_path_case("c:WiNdOws") == "C:Windows");
    CHECK(win32_fix_path_case("c:WiNdOws/system32") == "C:Windows\\System32");
    real_filesystem.current_path(original_cwd, VCPKG_LINE_INFO);

    real_filesystem.create_directories("SuB/Dir/Ectory", VCPKG_LINE_INFO);
    CHECK(win32_fix_path_case("sub") == "SuB");
    CHECK(win32_fix_path_case("SUB") == "SuB");
    CHECK(win32_fix_path_case("sub/") == "SuB\\");
    CHECK(win32_fix_path_case("sub/dir") == "SuB\\Dir");
    CHECK(win32_fix_path_case("sub/dir/") == "SuB\\Dir\\");
    CHECK(win32_fix_path_case("sub/dir/ectory") == "SuB\\Dir\\Ectory");
    CHECK(win32_fix_path_case("sub/dir/ectory/") == "SuB\\Dir\\Ectory\\");
    real_filesystem.remove_all("SuB", VCPKG_LINE_INFO);

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
        const Filesystem& fs;

        void operator()(Catch::Benchmark::Chronometer& meter, std::uint32_t max_depth) const
        {
            std::vector<Path> temp_dirs;
            temp_dirs.resize(meter.runs());

            std::generate(begin(temp_dirs), end(temp_dirs), [&] {
                Path temp_dir = base_temporary_directory() / get_random_filename(urbg, "_remove_all_bench");
                create_directory_tree(urbg, fs, temp_dir, max_depth);
                return temp_dir;
            });

            meter.measure([&](int run) {
                std::error_code ec;
                Path fp;
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
