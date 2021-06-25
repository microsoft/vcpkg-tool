#include <catch2/catch.hpp>

#include <vcpkg/base/files.h>
#include <vcpkg/base/strings.h>

#include <iostream>
#include <random>
#include <vector>

#include <vcpkg-test/util.h>

using vcpkg::Test::AllowSymlinks;
using vcpkg::Test::base_temporary_directory;
using vcpkg::Test::can_create_symlinks;

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
    using uid_t = std::uniform_int_distribution<std::uint64_t>;
    using urbg_t = std::mt19937_64;

    urbg_t get_urbg(std::uint64_t index)
    {
        // smallest prime > 2**63 - 1
        return urbg_t{index + 9223372036854775837ULL};
    }

    std::string get_random_filename(urbg_t& urbg) { return vcpkg::Strings::b32_encode(uid_t{}(urbg)); }

    struct MaxDepth
    {
        std::uint64_t i;
        explicit MaxDepth(std::uint64_t i) : i(i) { }
        operator uint64_t() const { return i; }
    };

    struct Width
    {
        std::uint64_t i;
        explicit Width(std::uint64_t i) : i(i) { }
        operator uint64_t() const { return i; }
    };

    struct CurrentDepth
    {
        std::uint64_t i;
        explicit CurrentDepth(std::uint64_t i) : i(i) { }
        operator uint64_t() const { return i; }
        CurrentDepth incremented() const { return CurrentDepth{i + 1}; }
    };

    void create_directory_tree(urbg_t& urbg,
                               vcpkg::Filesystem& fs,
                               const vcpkg::path& base,
                               MaxDepth max_depth,
                               AllowSymlinks allow_symlinks = AllowSymlinks::Yes,
                               Width width = Width{5},
                               CurrentDepth current_depth = CurrentDepth{0})
    {
        // we want ~70% of our "files" to be directories, and then a third
        // each of the remaining ~30% to be regular files, directory symlinks,
        // and regular symlinks
        constexpr std::uint64_t directory_min_tag = 0;
        constexpr std::uint64_t directory_max_tag = 6;
        constexpr std::uint64_t regular_file_tag = 7;
        constexpr std::uint64_t regular_symlink_tag = 8;
        constexpr std::uint64_t directory_symlink_tag = 9;

        allow_symlinks = AllowSymlinks{allow_symlinks && can_create_symlinks()};

        // if we're at the max depth, we only want to build non-directories
        std::uint64_t file_type;
        if (current_depth >= max_depth)
        {
            file_type = uid_t{regular_file_tag, directory_symlink_tag}(urbg);
        }
        else if (current_depth < 2)
        {
            file_type = directory_min_tag;
        }
        else
        {
            file_type = uid_t{directory_min_tag, regular_symlink_tag}(urbg);
        }

        if (!allow_symlinks && file_type > regular_file_tag)
        {
            file_type = regular_file_tag;
        }

        std::error_code ec;
        if (file_type <= directory_max_tag)
        {
            fs.create_directory(base, ec);
            if (ec)
            {
                CHECK_EC_ON_FILE(base, ec);
            }

            for (std::uint64_t i = 0; i < width; ++i)
            {
                create_directory_tree(urbg,
                                      fs,
                                      base / get_random_filename(urbg),
                                      max_depth,
                                      allow_symlinks,
                                      width,
                                      current_depth.incremented());
            }
        }
        else if (file_type == regular_file_tag)
        {
            // regular file
            fs.write_contents(base, "", ec);
        }
        else if (file_type == regular_symlink_tag)
        {
            // regular symlink
            auto base_link = base;
            base_link.replace_filename(vcpkg::u8string(base.filename()) + "-orig");
            fs.write_contents(base_link, "", ec);
            CHECK_EC_ON_FILE(base_link, ec);
            vcpkg::Test::create_symlink(base_link, base, ec);
        }
        else // file_type == directory_symlink_tag
        {
            // directory symlink
            auto parent = base;
            parent.remove_filename();
            vcpkg::Test::create_directory_symlink(parent, base, ec);
        }

        CHECK_EC_ON_FILE(base, ec);
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
    auto urbg = get_urbg(0);

    auto& fs = setup();

    vcpkg::path temp_dir = base_temporary_directory() / get_random_filename(urbg);
    INFO("temp dir is: " << temp_dir);

    create_directory_tree(urbg, fs, temp_dir, MaxDepth{5});

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
    auto urbg = get_urbg(1);
    auto& fs = setup();

    struct
    {
        urbg_t& urbg;
        vcpkg::Filesystem& fs;

        void operator()(Catch::Benchmark::Chronometer& meter, MaxDepth max_depth, AllowSymlinks allow_symlinks) const
        {
            std::vector<path> temp_dirs;
            temp_dirs.resize(meter.runs());

            std::generate(begin(temp_dirs), end(temp_dirs), [&] {
                path temp_dir = base_temporary_directory() / get_random_filename(urbg);
                create_directory_tree(urbg, fs, temp_dir, max_depth, allow_symlinks);
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

    BENCHMARK_ADVANCED("small directory, no symlinks")(Catch::Benchmark::Chronometer meter)
    {
        do_benchmark(meter, MaxDepth{2}, AllowSymlinks::No);
    };

    BENCHMARK_ADVANCED("large directory, no symlinks")(Catch::Benchmark::Chronometer meter)
    {
        do_benchmark(meter, MaxDepth{5}, AllowSymlinks::No);
    };

    if (can_create_symlinks())
    {
        BENCHMARK_ADVANCED("small directory, symlinks")(Catch::Benchmark::Chronometer meter)
        {
            do_benchmark(meter, MaxDepth{2}, AllowSymlinks::Yes);
        };

        BENCHMARK_ADVANCED("large directory, symlinks")(Catch::Benchmark::Chronometer meter)
        {
            do_benchmark(meter, MaxDepth{5}, AllowSymlinks::Yes);
        };
    }
}
#endif
