#include <vcpkg-test/util.h>

#include <vcpkg/base/git.h>

using namespace vcpkg;

TEST_CASE ("parse_git_diff_tree_line", "[git]")
{
    // clang-format off
    static constexpr StringView test_data = StringLiteral{
        // I got git to actually make these:
        ":000000 100644 0000000000000000000000000000000000000000 b803c06aa6827aea93ef945b70b8e27b1765c5c5 A\0file-added\0"
        ":100644 100644 349333663b3732eb5c35d2fc861563e370e2743b 349333663b3732eb5c35d2fc861563e370e2743b C100\0file-copied-old\0file-copied-new\0"
        ":100644 100644 349333663b3732eb5c35d2fc861563e370e2743b 80e050b8e009e815b9dd3a87cad0dd0fac6d1bfd M\0file-copied-old\0"
        ":100644 000000 bce58a758fe8300f9057f9831a591e87b0f30a18 0000000000000000000000000000000000000000 D\0file-deleted\0"
        ":100644 100644 41292b2464bdbc937b607925ebb8f5ce33cca677 d1a55cdac311209ef5bcbbef8a2ab872d26fc089 M\0file-modified\0"
        ":100644 100644 db6a36d77c14fc2ede2a34f0cc638b6692a9ca3c db6a36d77c14fc2ede2a34f0cc638b6692a9ca3c R100\0file-moved-old\0file-moved-new\0"
        // I made this one up to artificially create a T:
        ":100644 100644 41292b2464bdbc937b607925ebb8f5ce33cca677 d1a55cdac311209ef5bcbbef8a2ab872d26fc089 T\0file-type-modified\0"
        // I made this one up to artificially create a U:
        ":100644 100644 41292b2464bdbc937b607925ebb8f5ce33cca677 d1a55cdac311209ef5bcbbef8a2ab872d26fc089 U\0file-unmerged\0"
        // I made this one up to artificially create an X:
        ":100644 100644 41292b2464bdbc937b607925ebb8f5ce33cca677 d1a55cdac311209ef5bcbbef8a2ab872d26fc089 X\0file-unknown\0"
        // I made this one up to artificially create an M with a score:
        ":100644 100644 41292b2464bdbc937b607925ebb8f5ce33cca677 d1a55cdac311209ef5bcbbef8a2ab872d26fc089 M10\0file-modified-score\0"
        "\0" // extra null to test nonempty range leftover
        };
    // clang-format on

    std::vector<GitDiffTreeLine> test_out;
    auto first = test_data.begin();
    const auto last = test_data.end();

    REQUIRE(parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 1);
    REQUIRE(test_out.back().old_mode == "000000");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "0000000000000000000000000000000000000000");
    REQUIRE(test_out.back().new_sha == "b803c06aa6827aea93ef945b70b8e27b1765c5c5");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Added);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-added");
    REQUIRE(test_out.back().old_file_name.empty());

    REQUIRE(parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 2);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "349333663b3732eb5c35d2fc861563e370e2743b");
    REQUIRE(test_out.back().new_sha == "349333663b3732eb5c35d2fc861563e370e2743b");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Copied);
    REQUIRE(test_out.back().score == 100);
    REQUIRE(test_out.back().file_name == "file-copied-new");
    REQUIRE(test_out.back().old_file_name == "file-copied-old");

    REQUIRE(parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 3);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "349333663b3732eb5c35d2fc861563e370e2743b");
    REQUIRE(test_out.back().new_sha == "80e050b8e009e815b9dd3a87cad0dd0fac6d1bfd");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Modified);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-copied-old");
    REQUIRE(test_out.back().old_file_name.empty());

    REQUIRE(parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 4);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "000000");
    REQUIRE(test_out.back().old_sha == "bce58a758fe8300f9057f9831a591e87b0f30a18");
    REQUIRE(test_out.back().new_sha == "0000000000000000000000000000000000000000");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Deleted);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-deleted");
    REQUIRE(test_out.back().old_file_name.empty());

    REQUIRE(parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 5);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "41292b2464bdbc937b607925ebb8f5ce33cca677");
    REQUIRE(test_out.back().new_sha == "d1a55cdac311209ef5bcbbef8a2ab872d26fc089");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Modified);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-modified");
    REQUIRE(test_out.back().old_file_name.empty());

    REQUIRE(parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 6);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "db6a36d77c14fc2ede2a34f0cc638b6692a9ca3c");
    REQUIRE(test_out.back().new_sha == "db6a36d77c14fc2ede2a34f0cc638b6692a9ca3c");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Renamed);
    REQUIRE(test_out.back().score == 100);
    REQUIRE(test_out.back().file_name == "file-moved-new");
    REQUIRE(test_out.back().old_file_name == "file-moved-old");

    REQUIRE(parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 7);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "41292b2464bdbc937b607925ebb8f5ce33cca677");
    REQUIRE(test_out.back().new_sha == "d1a55cdac311209ef5bcbbef8a2ab872d26fc089");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::TypeChange);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-type-modified");
    REQUIRE(test_out.back().old_file_name.empty());

    REQUIRE(parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 8);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "41292b2464bdbc937b607925ebb8f5ce33cca677");
    REQUIRE(test_out.back().new_sha == "d1a55cdac311209ef5bcbbef8a2ab872d26fc089");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Unmerged);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-unmerged");
    REQUIRE(test_out.back().old_file_name.empty());

    REQUIRE(parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 9);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "41292b2464bdbc937b607925ebb8f5ce33cca677");
    REQUIRE(test_out.back().new_sha == "d1a55cdac311209ef5bcbbef8a2ab872d26fc089");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Unknown);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-unknown");
    REQUIRE(test_out.back().old_file_name.empty());

    REQUIRE(parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 10);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "41292b2464bdbc937b607925ebb8f5ce33cca677");
    REQUIRE(test_out.back().new_sha == "d1a55cdac311209ef5bcbbef8a2ab872d26fc089");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Modified);
    REQUIRE(test_out.back().score == 10);
    REQUIRE(test_out.back().file_name == "file-modified-score");
    REQUIRE(test_out.back().old_file_name.empty());

    REQUIRE(first + 1 == last);
    REQUIRE(!parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 10);
    REQUIRE(first + 1 == last);

    FullyBufferedDiagnosticContext bdc;
    REQUIRE(parse_git_diff_tree_lines(bdc, "git diff-tree", test_data.substr(0, test_data.size() - 1))
                .value_or_exit(VCPKG_LINE_INFO) == test_out);
    REQUIRE(bdc.empty());
}

TEST_CASE ("parse_git_diff_tree_line_failures", "[git]")
{
    std::vector<GitDiffTreeLine> test_out;

    // Too short
    static constexpr StringLiteral test_short = ":100644";
    auto first = test_short.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_short.end()));
    REQUIRE(first == test_short.begin());

    // Missing colon
    static constexpr StringLiteral test_no_colon = "100644 100644 abcd123 1234567 M\0file1\0";
    first = test_no_colon.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_no_colon.end()));
    REQUIRE(first == test_no_colon.begin());

    // Incorrect spacing at position 7
    static constexpr StringLiteral test_bad_space1 = ":100644X100644 abcd123 1234567 M\0file1\0";
    first = test_bad_space1.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_bad_space1.end()));
    REQUIRE(first == test_bad_space1.begin());

    // Incorrect spacing at position 14
    static constexpr StringLiteral test_bad_space2 = ":100644 100644Xabcd123 1234567 M\0file1\0";
    first = test_bad_space2.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_bad_space2.end()));
    REQUIRE(first == test_bad_space2.begin());

    // Incorrect spacing at position 55 (after SHA)
    static constexpr StringLiteral test_bad_space3 =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123Xabcd123abcd123abcd123abcd123abcd123 M\0file1\0";
    first = test_bad_space3.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_bad_space3.end()));
    REQUIRE(first == test_bad_space3.begin());

    // Incorrect spacing at position 96 (after second SHA)
    static constexpr StringLiteral test_bad_space4 =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123XM\0file1\0";
    first = test_bad_space4.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_bad_space4.end()));
    REQUIRE(first == test_bad_space4.begin());

    // Using 'Z' as an invalid action character
    static constexpr StringLiteral test_invalid_action =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 Z\0file1\0";
    first = test_invalid_action.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_invalid_action.end()));
    REQUIRE(first == test_invalid_action.begin());

    // Not a mode
    static constexpr StringLiteral test_not_mode_1 =
        ":100a44 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 A\0file1\0";
    first = test_not_mode_1.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_not_mode_1.end()));
    REQUIRE(first == test_not_mode_1.begin());

    static constexpr StringLiteral test_not_mode_2 =
        ":100644 10a644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 A\0file1\0";
    first = test_not_mode_2.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_not_mode_2.end()));
    REQUIRE(first == test_not_mode_2.begin());

    // Not a SHA
    static constexpr StringLiteral test_not_sha_1 =
        ":100644 100644 abcd123abcd123abcd12zabcd123abcd123 abcd123abcd123abcd123abcd123abcd123 A\0file1\0";
    first = test_not_sha_1.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_not_sha_1.end()));
    REQUIRE(first == test_not_sha_1.begin());

    static constexpr StringLiteral test_not_sha_2 =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcdz23abcd123abcd123 A\0file1\0";
    first = test_not_sha_2.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_not_sha_2.end()));
    REQUIRE(first == test_not_sha_2.begin());

    // Score with no terminator
    static constexpr StringLiteral test_missing_score_term =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 M50";
    first = test_missing_score_term.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_missing_score_term.end()));
    REQUIRE(first == test_missing_score_term.begin());

    // Score is not a valid integer
    static constexpr StringLiteral test_invalid_score =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 M5x\0file1\0";
    first = test_invalid_score.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_invalid_score.end()));
    REQUIRE(first == test_invalid_score.begin());

    // Rename action with missing terminator after first file name
    static constexpr StringLiteral test_missing_file_term =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 R86\0file1";
    first = test_missing_file_term.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_missing_file_term.end()));
    REQUIRE(first == test_missing_file_term.begin());

    // Copy action with missing terminator after first file name
    static constexpr StringLiteral test_missing_file_term2 =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 C68\0file1";
    first = test_missing_file_term2.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_missing_file_term2.end()));
    REQUIRE(first == test_missing_file_term2.begin());

    // Missing terminator after file name
    static constexpr StringLiteral test_missing_term =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 M\0file1";
    first = test_missing_term.begin();
    REQUIRE(!parse_git_diff_tree_line(test_out, first, test_missing_term.end()));
    REQUIRE(first == test_missing_term.begin());
}
