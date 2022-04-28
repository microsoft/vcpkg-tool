#include <catch2/catch.hpp>

#include <vcpkg/base/git.h>

using namespace vcpkg;

TEST_CASE ("Parse git status output", "[git]")
{
    using Status = GitStatusLine::Status;

    static constexpr StringLiteral empty_output = "";
    static constexpr StringLiteral good_output = " A ports/testport/vcpkg.json\n"
                                                 "D  ports/testport/CONTROL\n"
                                                 "?! versions/t-/testport.json\n"
                                                 " R ports/testport/fix.patch -> ports/testport/fix-cmake-config.patch";
    static constexpr StringLiteral bad_output = "git failed to execute command";
    static constexpr StringLiteral bad_output2 = " A \n"
                                                 "ports/testport/vcpkg.json";
    static constexpr StringLiteral bad_output3 = "A* ports/testport/vcpkg.json";

    auto maybe_empty_results = parse_git_status_output(empty_output);
    REQUIRE(maybe_empty_results.has_value());
    CHECK(maybe_empty_results.value_or_exit(VCPKG_LINE_INFO).empty());

    auto maybe_good_results = parse_git_status_output(good_output);
    REQUIRE(maybe_good_results.has_value());
    auto good_results = maybe_good_results.value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(good_results.size() == 4);

    CHECK(good_results[0].index_status == Status::Unmodified);
    CHECK(good_results[0].work_tree_status == Status::Added);
    CHECK(good_results[0].path == "ports/testport/vcpkg.json");
    CHECK(good_results[0].old_path.empty());

    CHECK(good_results[1].index_status == Status::Deleted);
    CHECK(good_results[1].work_tree_status == Status::Unmodified);
    CHECK(good_results[1].path == "ports/testport/CONTROL");
    CHECK(good_results[1].old_path.empty());

    CHECK(good_results[2].index_status == Status::Untracked);
    CHECK(good_results[2].work_tree_status == Status::Ignored);
    CHECK(good_results[2].path == "versions/t-/testport.json");
    CHECK(good_results[2].old_path.empty());

    CHECK(good_results[3].index_status == Status::Unmodified);
    CHECK(good_results[3].work_tree_status == Status::Renamed);
    CHECK(good_results[3].path == "ports/testport/fix-cmake-config.patch");
    CHECK(good_results[3].old_path == "ports/testport/fix.patch");

    auto maybe_bad_results = parse_git_status_output(bad_output);
    CHECK(!maybe_bad_results.has_value());

    auto maybe_bad_results2 = parse_git_status_output(bad_output2);
    CHECK(!maybe_bad_results2.has_value());

    auto maybe_bad_results3 = parse_git_status_output(bad_output3);
    CHECK(!maybe_bad_results3.has_value());
}

TEST_CASE ("Parse git ls-tree output", "[git]")
{
    static constexpr StringLiteral empty_output = "";
    static constexpr StringLiteral good_output =
        R"(100644 blob d4d1bb19588d72aa1eba78d2856dc051907dbf67 README.md 
040000 tree 1c706a0a8580545fa17ea044b93f2cd91406d9c6 ports )";
    static constexpr StringLiteral bad_output = "this is an error";
    static constexpr StringLiteral bad_output2 = "100644 notatype d4d1bb19588d72aa1eba78d2856dc051907dbf67 README.md";
    static constexpr StringLiteral bad_output3 = "100644 blob d4d1bb1 README.md";

    auto maybe_empty_results = parse_git_ls_tree_output(empty_output);
    REQUIRE(maybe_empty_results.has_value());
    const auto& empty_results = *maybe_empty_results.get();
    CHECK(empty_results.empty());

    auto maybe_good_results = parse_git_ls_tree_output(good_output);
    REQUIRE(maybe_good_results.has_value());
    const auto& good_results = *maybe_good_results.get();
    CHECK(good_results.size() == 2);
    CHECK(good_results[0].mode == "100644");
    CHECK(good_results[0].type == "blob");
    CHECK(good_results[0].git_object == "d4d1bb19588d72aa1eba78d2856dc051907dbf67");
    CHECK(good_results[0].path == "README.md");
    CHECK(good_results[1].mode == "040000");
    CHECK(good_results[1].type == "tree");
    CHECK(good_results[1].git_object == "1c706a0a8580545fa17ea044b93f2cd91406d9c6");
    CHECK(good_results[1].path == "ports");

    auto maybe_bad_results = parse_git_ls_tree_output(bad_output);
    CHECK(!maybe_bad_results.has_value());

    auto maybe_bad_results2 = parse_git_ls_tree_output(bad_output2);
    CHECK(!maybe_bad_results2.has_value());

    auto maybe_bad_results3 = parse_git_ls_tree_output(bad_output3);
    CHECK(!maybe_bad_results3.has_value());
}

TEST_CASE ("Extract port name from path", "[git]")
{
    // should be empty cases
    CHECK(try_extract_port_name_from_path("ports/").empty());
    CHECK(try_extract_port_name_from_path("ports/README.md").empty());
    CHECK(try_extract_port_name_from_path("versions/test/test.json").empty());
    CHECK(try_extract_port_name_from_path("overlays/ports/test/portfile.cmake").empty());

    // should have port name cases
    CHECK(try_extract_port_name_from_path("ports/t/CONTROL") == "t");
    CHECK(try_extract_port_name_from_path("ports/test/vcpkg.json") == "test");
    CHECK(try_extract_port_name_from_path("ports/ports/a/README.md") == "ports");
    CHECK(try_extract_port_name_from_path("ports/ports/a/ports/b/ports/c.json") == "ports");
}