#include <vcpkg/base/system_headers.h>

#include <catch2/catch.hpp>

#include <vcpkg/base/files.h>
#include <vcpkg/base/strings.h>

#include <iostream>
#include <random>
#include <vector>

#include <vcpkg-test/util.h>

using vcpkg::Test::base_temporary_directory;

#define CHECK_EC_ON_FILE(file, ec)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (ec)                                                                                                        \
        {                                                                                                              \
            FAIL(file << ": " << ec.message());                                                                        \
        }                                                                                                              \
    } while (0)

namespace
{
    using urbg_t = std::mt19937_64;

    std::string get_random_filename(urbg_t& urbg) { return vcpkg::Strings::b32_encode(urbg()); }

#if defined(_WIN32)
    bool is_valid_symlink_failure(const std::error_code& ec) noexcept
    {
        // on Windows, creating symlinks requires admin rights, so we ignore such failures
        return ec == std::error_code(ERROR_PRIVILEGE_NOT_HELD, std::system_category());
    }
#endif // ^^^ _WIN32

    void create_directory_tree(urbg_t& urbg,
                               vcpkg::Filesystem& fs,
                               const vcpkg::path& base,
                               std::uint32_t remaining_depth = 5)
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
            auto base_target = base;
            base_target.replace_filename(vcpkg::u8path(vcpkg::u8string(base.filename()) + "-target"));
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
                    FAIL(base << ": " << ec.message());
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
                    FAIL(base << ": " << ec.message());
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

        REQUIRE(vcpkg::exists(fs.symlink_status(base, ec)));
        CHECK_EC_ON_FILE(base, ec);
    }

    vcpkg::Filesystem& setup()
    {
        auto& fs = vcpkg::get_real_filesystem();

        std::error_code ec;
        fs.create_directory(base_temporary_directory(), ec);
        CHECK_EC_ON_FILE(base_temporary_directory(), ec);

        return fs;
    }
}

TEST_CASE ("vcpkg::combine works correctly", "[filesystem][files]")
{
    using namespace vcpkg;
    CHECK(combine(u8path("/a/b"), u8path("c/d")) == u8path("/a/b/c/d"));
    CHECK(combine(u8path("a/b"), u8path("c/d")) == u8path("a/b/c/d"));
    CHECK(combine(u8path("/a/b"), u8path("/c/d")) == u8path("/c/d"));

#if defined(_WIN32)
    CHECK(combine(u8path("C:/a/b"), u8path("c/d")) == u8path("C:/a/b/c/d"));
    CHECK(combine(u8path("C:a/b"), u8path("c/d")) == u8path("C:a/b/c/d"));
    CHECK(combine(u8path("C:a/b"), u8path("/c/d")) == u8path("C:/c/d"));
    CHECK(combine(u8path("C:/a/b"), u8path("/c/d")) == u8path("C:/c/d"));
    CHECK(combine(u8path("C:/a/b"), u8path("D:/c/d")) == u8path("D:/c/d"));
    CHECK(combine(u8path("C:/a/b"), u8path("D:c/d")) == u8path("D:c/d"));
    CHECK(combine(u8path("C:/a/b"), u8path("C:c/d")) == u8path("C:/a/b/c/d"));
#endif
}

TEST_CASE ("remove all", "[files]")
{
    urbg_t urbg;

    auto& fs = setup();

    vcpkg::path temp_dir = base_temporary_directory() / get_random_filename(urbg);
    INFO("temp dir is: " << temp_dir);

    create_directory_tree(urbg, fs, temp_dir);

    std::error_code ec;
    vcpkg::path fp;
    fs.remove_all(temp_dir, ec, fp);
    CHECK_EC_ON_FILE(fp, ec);

    REQUIRE_FALSE(fs.exists(temp_dir, ec));
    CHECK_EC_ON_FILE(temp_dir, ec);
}

TEST_CASE ("lexically_normal", "[files]")
{
    const auto lexically_normal = [](const char* s) { return vcpkg::lexically_normal(vcpkg::u8path(s)); };
    const auto native = [](const char* s) { return std::move(vcpkg::u8path(s).make_preferred()); };
    CHECK(vcpkg::lexically_normal(vcpkg::path()).native() == vcpkg::path().native());

    // these test cases are taken from the MS STL tests
    CHECK(lexically_normal("cat/./dog/..").native() == native("cat/").native());
    CHECK(lexically_normal("cat/.///dog/../").native() == native("cat/").native());

    CHECK(lexically_normal("cat/./dog/..").native() == native("cat/").native());
    CHECK(lexically_normal("cat/.///dog/../").native() == native("cat/").native());

    CHECK(lexically_normal(".").native() == native(".").native());
    CHECK(lexically_normal("./").native() == native(".").native());
    CHECK(lexically_normal("./.").native() == native(".").native());
    CHECK(lexically_normal("././").native() == native(".").native());

    CHECK(lexically_normal("../../..").native() == native("../../..").native());
    CHECK(lexically_normal("../../../").native() == native("../../..").native());

    CHECK(lexically_normal("../../../a/b/c").native() == native("../../../a/b/c").native());

    CHECK(lexically_normal("/../../..").native() == native("/").native());
    CHECK(lexically_normal("/../../../").native() == native("/").native());

    CHECK(lexically_normal("/../../../a/b/c").native() == native("/a/b/c").native());

    CHECK(lexically_normal("a/..").native() == native(".").native());
    CHECK(lexically_normal("a/../").native() == native(".").native());

#if defined(_WIN32)
    CHECK(lexically_normal(R"(X:)").native() == LR"(X:)");

    CHECK(lexically_normal(R"(X:DriveRelative)").native() == LR"(X:DriveRelative)");

    CHECK(lexically_normal(R"(X:\)").native() == LR"(X:\)");
    CHECK(lexically_normal(R"(X:/)").native() == LR"(X:\)");
    CHECK(lexically_normal(R"(X:\\\)").native() == LR"(X:\)");
    CHECK(lexically_normal(R"(X:///)").native() == LR"(X:\)");

    CHECK(lexically_normal(R"(X:\DosAbsolute)").native() == LR"(X:\DosAbsolute)");
    CHECK(lexically_normal(R"(X:/DosAbsolute)").native() == LR"(X:\DosAbsolute)");
    CHECK(lexically_normal(R"(X:\\\DosAbsolute)").native() == LR"(X:\DosAbsolute)");
    CHECK(lexically_normal(R"(X:///DosAbsolute)").native() == LR"(X:\DosAbsolute)");

    CHECK(lexically_normal(R"(\RootRelative)").native() == LR"(\RootRelative)");
    CHECK(lexically_normal(R"(/RootRelative)").native() == LR"(\RootRelative)");
    CHECK(lexically_normal(R"(\\\RootRelative)").native() == LR"(\RootRelative)");
    CHECK(lexically_normal(R"(///RootRelative)").native() == LR"(\RootRelative)");

    CHECK(lexically_normal(R"(\\server\share)").native() == LR"(\\server\share)");
    CHECK(lexically_normal(R"(//server/share)").native() == LR"(\\server\share)");
    CHECK(lexically_normal(R"(\\server\\\share)").native() == LR"(\\server\share)");
    CHECK(lexically_normal(R"(//server///share)").native() == LR"(\\server\share)");

    CHECK(lexically_normal(R"(\\?\device)").native() == LR"(\\?\device)");
    CHECK(lexically_normal(R"(//?/device)").native() == LR"(\\?\device)");

    CHECK(lexically_normal(R"(\??\device)").native() == LR"(\??\device)");
    CHECK(lexically_normal(R"(/??/device)").native() == LR"(\??\device)");

    CHECK(lexically_normal(R"(\\.\device)").native() == LR"(\\.\device)");
    CHECK(lexically_normal(R"(//./device)").native() == LR"(\\.\device)");

    CHECK(lexically_normal(R"(\\?\UNC\server\share)").native() == LR"(\\?\UNC\server\share)");
    CHECK(lexically_normal(R"(//?/UNC/server/share)").native() == LR"(\\?\UNC\server\share)");

    CHECK(lexically_normal(R"(C:\a/b\\c\/d/\e//f)").native() == LR"(C:\a\b\c\d\e\f)");

    CHECK(lexically_normal(R"(C:\meow\)").native() == LR"(C:\meow\)");
    CHECK(lexically_normal(R"(C:\meow/)").native() == LR"(C:\meow\)");
    CHECK(lexically_normal(R"(C:\meow\\)").native() == LR"(C:\meow\)");
    CHECK(lexically_normal(R"(C:\meow\/)").native() == LR"(C:\meow\)");
    CHECK(lexically_normal(R"(C:\meow/\)").native() == LR"(C:\meow\)");
    CHECK(lexically_normal(R"(C:\meow//)").native() == LR"(C:\meow\)");

    CHECK(lexically_normal(R"(C:\a\.\b\.\.\c\.\.\.)").native() == LR"(C:\a\b\c\)");
    CHECK(lexically_normal(R"(C:\a\.\b\.\.\c\.\.\.\)").native() == LR"(C:\a\b\c\)");

    CHECK(lexically_normal(R"(C:\a\b\c\d\e\..\f\..\..\..\g\h)").native() == LR"(C:\a\b\g\h)");

    CHECK(lexically_normal(R"(C:\a\b\c\d\e\..\f\..\..\..\g\h\..)").native() == LR"(C:\a\b\g\)");
    CHECK(lexically_normal(R"(C:\a\b\c\d\e\..\f\..\..\..\g\h\..\)").native() == LR"(C:\a\b\g\)");
    CHECK(lexically_normal(
              R"(/\server/\share/\a/\b/\c/\./\./\d/\../\../\../\../\../\../\../\other/x/y/z/.././..\meow.txt)")
              .native() == LR"(\\server\other\x\meow.txt)");
#endif
}

TEST_CASE ("LinesCollector", "[files]")
{
    using vcpkg::Strings::LinesCollector;
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
    using vcpkg::win32_fix_path_case;

    // This test assumes that the Windows directory is C:\Windows

    CHECK(win32_fix_path_case(L"") == L"");

    CHECK(win32_fix_path_case(L"C:") == L"C:");
    CHECK(win32_fix_path_case(L"c:") == L"C:");
    CHECK(win32_fix_path_case(L"C:/") == L"C:\\");
    CHECK(win32_fix_path_case(L"C:\\") == L"C:\\");
    CHECK(win32_fix_path_case(L"c:\\") == L"C:\\");
    CHECK(win32_fix_path_case(L"C:\\WiNdOws") == L"C:\\Windows");
    CHECK(win32_fix_path_case(L"c:\\WiNdOws\\") == L"C:\\Windows\\");
    CHECK(win32_fix_path_case(L"C://///////WiNdOws") == L"C:\\Windows");
    CHECK(win32_fix_path_case(L"c:\\/\\/WiNdOws\\/") == L"C:\\Windows\\");

    auto& fs = vcpkg::get_real_filesystem();
    auto original_cwd = fs.current_path(VCPKG_LINE_INFO);
    fs.current_path(L"C:\\", VCPKG_LINE_INFO);
    CHECK(win32_fix_path_case(L"\\") == L"\\");
    CHECK(win32_fix_path_case(L"\\/\\WiNdOws") == L"\\Windows");
    CHECK(win32_fix_path_case(L"\\WiNdOws") == L"\\Windows");
    CHECK(win32_fix_path_case(L"\\WiNdOws") == L"\\Windows");
    CHECK(win32_fix_path_case(L"c:WiNdOws") == L"C:Windows");
    CHECK(win32_fix_path_case(L"c:WiNdOws/system32") == L"C:Windows\\System32");
    fs.current_path(original_cwd, VCPKG_LINE_INFO);

    fs.create_directories("SuB/Dir/Ectory", VCPKG_LINE_INFO);
    CHECK(win32_fix_path_case(L"sub") == L"SuB");
    CHECK(win32_fix_path_case(L"SUB") == L"SuB");
    CHECK(win32_fix_path_case(L"sub/") == L"SuB\\");
    CHECK(win32_fix_path_case(L"sub/dir") == L"SuB\\Dir");
    CHECK(win32_fix_path_case(L"sub/dir/") == L"SuB\\Dir\\");
    CHECK(win32_fix_path_case(L"sub/dir/ectory") == L"SuB\\Dir\\Ectory");
    CHECK(win32_fix_path_case(L"sub/dir/ectory/") == L"SuB\\Dir\\Ectory\\");
    fs.remove_all("SuB", VCPKG_LINE_INFO);

    CHECK(win32_fix_path_case(L"//nonexistent_server\\nonexistent_share\\") ==
          L"\\\\nonexistent_server\\nonexistent_share\\");
    CHECK(win32_fix_path_case(L"\\\\nonexistent_server\\nonexistent_share\\") ==
          L"\\\\nonexistent_server\\nonexistent_share\\");
    CHECK(win32_fix_path_case(L"\\\\nonexistent_server\\nonexistent_share") ==
          L"\\\\nonexistent_server\\nonexistent_share");

    CHECK(win32_fix_path_case(L"///three_slashes_not_a_server\\subdir\\") == L"\\three_slashes_not_a_server\\subdir\\");

    CHECK(win32_fix_path_case(L"\\??\\c:\\WiNdOws") == L"\\??\\c:\\WiNdOws");
    CHECK(win32_fix_path_case(L"\\\\?\\c:\\WiNdOws") == L"\\\\?\\c:\\WiNdOws");
    CHECK(win32_fix_path_case(L"\\\\.\\c:\\WiNdOws") == L"\\\\.\\c:\\WiNdOws");
    CHECK(win32_fix_path_case(L"c:\\/\\/Nonexistent\\/path/here") == L"C:\\Nonexistent\\path\\here");
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
        vcpkg::Filesystem& fs;

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
