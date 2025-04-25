#include <vcpkg-test/util.h>

#include <vcpkg/base/git.h>

using namespace vcpkg;

TEST_CASE ("parse_git_ls_tree_output", "[git]")
{
    static constexpr StringLiteral test_data =
        StringLiteral{"100644 blob d0c3b3e9ccf66ddf0f30f2ac9a8a7f310c45b3d1\t.gitattributes\0"
                      "040000 tree 44246de64bc5e07c0e4ed90a66415f0c3742e1df\t.github\0"
                      "100644 blob d6d47929120f8f1d73457cfd5ee41d7e6c96bf0c\t.gitignore\0"
                      "100644 blob e69de29bb2d1d6434b8b29ae775ad8c2e48c5391\t.vcpkg-root\0"
                      "100644 blob 6e019d977590f5c602b24ddc1b6146e4d7d69a62\tCONTRIBUTING.md\0"
                      "100644 blob 63c4c885fbb4c313b09cbedc5b2afe97e0584ff9\tCONTRIBUTING_pt.md\0"
                      "100644 blob 7aef2f5dcb9fd82bad9becc44a5800494be85945\tCONTRIBUTING_zh.md\0"
                      "100644 blob 4d23e0e39b2531c41499e7b1c5ec0efffd15c6b6\tLICENSE.txt\0"
                      "100644 blob 0e2e9604835faba06f6e8bbf27b723f58ce35c95\tNOTICE.txt\0"
                      "100644 blob 678ca692eff279bc1c06f6467349a5166222a49e\tNOTICE_pt.txt\0"
                      "100644 blob 87ca864c4d975d72f00533edfc074ef58e3b930b\tREADME.md\0"
                      "100644 blob 869fdfe2b246991a053fab9cfec1bed3ab532ab1\tSECURITY.md\0"
                      "100644 blob 54e0b85a225030ab1a9e0096cdb6f637ee84c326\tbootstrap-vcpkg.bat\0"
                      "100755 blob 7165a725fd719883b614e6e90a95179dcd5a1817\tbootstrap-vcpkg.sh\0"
                      "040000 tree fe6e499f79b05d67cf23cd6d2d523108031380e0\tdocs\0"
                      "040000 tree 84aa6e15bbf46cf43d72e0e33eaa24a3d16db12a\tports\0"
                      "040000 tree 2b1772552342a6a3b6e723f359a2b3a42db0d630\tscripts\0"
                      "100644 blob 8b9f485e767b9fa3f0b7fd4875a5c35a51711cc3\tshell.nix\0"
                      "040000 tree 2699108f354764415c1484e3d09ab493b9ef2c51\ttoolsrc\0"
                      "040000 tree 2c82f61efda080f200d2ae079d68dfff9b4c08fe\ttriplets\0"
                      "040000 tree 0a65dad715e7c6fa94e5cd140ab094fb99211e63\tversions\0"};

    FullyBufferedDiagnosticContext bdc;
    std::vector<GitLSTreeEntry> test_out;
    REQUIRE(!parse_git_ls_tree_output(bdc, test_out, test_data, "git ls-tree HEAD:^{tree}"));
    std::vector<GitLSTreeEntry> expected = {
        {StringLiteral{".gitattributes"}, StringLiteral{"d0c3b3e9ccf66ddf0f30f2ac9a8a7f310c45b3d1"}},
        {StringLiteral{".github"}, StringLiteral{"44246de64bc5e07c0e4ed90a66415f0c3742e1df"}},
        {StringLiteral{".gitignore"}, StringLiteral{"d6d47929120f8f1d73457cfd5ee41d7e6c96bf0c"}},
        {StringLiteral{".vcpkg-root"}, StringLiteral{"e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"}},
        {StringLiteral{"CONTRIBUTING.md"}, StringLiteral{"6e019d977590f5c602b24ddc1b6146e4d7d69a62"}},
        {StringLiteral{"CONTRIBUTING_pt.md"}, StringLiteral{"63c4c885fbb4c313b09cbedc5b2afe97e0584ff9"}},
        {StringLiteral{"CONTRIBUTING_zh.md"}, StringLiteral{"7aef2f5dcb9fd82bad9becc44a5800494be85945"}},
        {StringLiteral{"LICENSE.txt"}, StringLiteral{"4d23e0e39b2531c41499e7b1c5ec0efffd15c6b6"}},
        {StringLiteral{"NOTICE.txt"}, StringLiteral{"0e2e9604835faba06f6e8bbf27b723f58ce35c95"}},
        {StringLiteral{"NOTICE_pt.txt"}, StringLiteral{"678ca692eff279bc1c06f6467349a5166222a49e"}},
        {StringLiteral{"README.md"}, StringLiteral{"87ca864c4d975d72f00533edfc074ef58e3b930b"}},
        {StringLiteral{"SECURITY.md"}, StringLiteral{"869fdfe2b246991a053fab9cfec1bed3ab532ab1"}},
        {StringLiteral{"bootstrap-vcpkg.bat"}, StringLiteral{"54e0b85a225030ab1a9e0096cdb6f637ee84c326"}},
        {StringLiteral{"bootstrap-vcpkg.sh"}, StringLiteral{"7165a725fd719883b614e6e90a95179dcd5a1817"}},
        {StringLiteral{"docs"}, StringLiteral{"fe6e499f79b05d67cf23cd6d2d523108031380e0"}},
        {StringLiteral{"ports"}, StringLiteral{"84aa6e15bbf46cf43d72e0e33eaa24a3d16db12a"}},
        {StringLiteral{"scripts"}, StringLiteral{"2b1772552342a6a3b6e723f359a2b3a42db0d630"}},
        {StringLiteral{"shell.nix"}, StringLiteral{"8b9f485e767b9fa3f0b7fd4875a5c35a51711cc3"}},
        {StringLiteral{"toolsrc"}, StringLiteral{"2699108f354764415c1484e3d09ab493b9ef2c51"}},
        {StringLiteral{"triplets"}, StringLiteral{"2c82f61efda080f200d2ae079d68dfff9b4c08fe"}},
        {StringLiteral{"versions"}, StringLiteral{"0a65dad715e7c6fa94e5cd140ab094fb99211e63"}},
    };

    REQUIRE(test_out == expected);
}

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

    first = parse_git_diff_tree_line(test_out, first, last);
    REQUIRE(first);
    REQUIRE(test_out.size() == 1);
    REQUIRE(test_out.back().old_mode == "000000");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "0000000000000000000000000000000000000000");
    REQUIRE(test_out.back().new_sha == "b803c06aa6827aea93ef945b70b8e27b1765c5c5");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Added);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-added");
    REQUIRE(test_out.back().old_file_name.empty());

    first = parse_git_diff_tree_line(test_out, first, last);
    REQUIRE(first);
    REQUIRE(test_out.size() == 2);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "349333663b3732eb5c35d2fc861563e370e2743b");
    REQUIRE(test_out.back().new_sha == "349333663b3732eb5c35d2fc861563e370e2743b");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Copied);
    REQUIRE(test_out.back().score == 100);
    REQUIRE(test_out.back().file_name == "file-copied-new");
    REQUIRE(test_out.back().old_file_name == "file-copied-old");

    first = parse_git_diff_tree_line(test_out, first, last);
    REQUIRE(first);
    REQUIRE(test_out.size() == 3);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "349333663b3732eb5c35d2fc861563e370e2743b");
    REQUIRE(test_out.back().new_sha == "80e050b8e009e815b9dd3a87cad0dd0fac6d1bfd");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Modified);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-copied-old");
    REQUIRE(test_out.back().old_file_name.empty());

    first = parse_git_diff_tree_line(test_out, first, last);
    REQUIRE(first);
    REQUIRE(test_out.size() == 4);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "000000");
    REQUIRE(test_out.back().old_sha == "bce58a758fe8300f9057f9831a591e87b0f30a18");
    REQUIRE(test_out.back().new_sha == "0000000000000000000000000000000000000000");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Deleted);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-deleted");
    REQUIRE(test_out.back().old_file_name.empty());

    first = parse_git_diff_tree_line(test_out, first, last);
    REQUIRE(first);
    REQUIRE(test_out.size() == 5);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "41292b2464bdbc937b607925ebb8f5ce33cca677");
    REQUIRE(test_out.back().new_sha == "d1a55cdac311209ef5bcbbef8a2ab872d26fc089");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Modified);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-modified");
    REQUIRE(test_out.back().old_file_name.empty());

    first = parse_git_diff_tree_line(test_out, first, last);
    REQUIRE(first);
    REQUIRE(test_out.size() == 6);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "db6a36d77c14fc2ede2a34f0cc638b6692a9ca3c");
    REQUIRE(test_out.back().new_sha == "db6a36d77c14fc2ede2a34f0cc638b6692a9ca3c");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Renamed);
    REQUIRE(test_out.back().score == 100);
    REQUIRE(test_out.back().file_name == "file-moved-new");
    REQUIRE(test_out.back().old_file_name == "file-moved-old");

    first = parse_git_diff_tree_line(test_out, first, last);
    REQUIRE(first);
    REQUIRE(test_out.size() == 7);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "41292b2464bdbc937b607925ebb8f5ce33cca677");
    REQUIRE(test_out.back().new_sha == "d1a55cdac311209ef5bcbbef8a2ab872d26fc089");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::TypeChange);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-type-modified");
    REQUIRE(test_out.back().old_file_name.empty());

    first = parse_git_diff_tree_line(test_out, first, last);
    REQUIRE(first);
    REQUIRE(test_out.size() == 8);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "41292b2464bdbc937b607925ebb8f5ce33cca677");
    REQUIRE(test_out.back().new_sha == "d1a55cdac311209ef5bcbbef8a2ab872d26fc089");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Unmerged);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-unmerged");
    REQUIRE(test_out.back().old_file_name.empty());

    first = parse_git_diff_tree_line(test_out, first, last);
    REQUIRE(first);
    REQUIRE(test_out.size() == 9);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "41292b2464bdbc937b607925ebb8f5ce33cca677");
    REQUIRE(test_out.back().new_sha == "d1a55cdac311209ef5bcbbef8a2ab872d26fc089");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Unknown);
    REQUIRE(test_out.back().score == 0);
    REQUIRE(test_out.back().file_name == "file-unknown");
    REQUIRE(test_out.back().old_file_name.empty());

    first = parse_git_diff_tree_line(test_out, first, last);
    REQUIRE(first);
    REQUIRE(first + 1 == last);
    REQUIRE(test_out.size() == 10);
    REQUIRE(test_out.back().old_mode == "100644");
    REQUIRE(test_out.back().new_mode == "100644");
    REQUIRE(test_out.back().old_sha == "41292b2464bdbc937b607925ebb8f5ce33cca677");
    REQUIRE(test_out.back().new_sha == "d1a55cdac311209ef5bcbbef8a2ab872d26fc089");
    REQUIRE(test_out.back().kind == GitDiffTreeLineKind::Modified);
    REQUIRE(test_out.back().score == 10);
    REQUIRE(test_out.back().file_name == "file-modified-score");
    REQUIRE(test_out.back().old_file_name.empty());

    REQUIRE(!parse_git_diff_tree_line(test_out, first, last));
    REQUIRE(test_out.size() == 10);

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
    REQUIRE(!parse_git_diff_tree_line(test_out, test_short.begin(), test_short.end()));

    // Missing colon
    static constexpr StringLiteral test_no_colon = "100644 100644 abcd123 1234567 M\0file1\0";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_no_colon.begin(), test_no_colon.end()));

    // Incorrect spacing at position 7
    static constexpr StringLiteral test_bad_space1 = ":100644X100644 abcd123 1234567 M\0file1\0";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_bad_space1.begin(), test_bad_space1.end()));

    // Incorrect spacing at position 14
    static constexpr StringLiteral test_bad_space2 = ":100644 100644Xabcd123 1234567 M\0file1\0";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_bad_space2.begin(), test_bad_space2.end()));

    // Incorrect spacing at position 55 (after SHA)
    static constexpr StringLiteral test_bad_space3 =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123Xabcd123abcd123abcd123abcd123abcd123 M\0file1\0";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_bad_space3.begin(), test_bad_space3.end()));

    // Incorrect spacing at position 96 (after second SHA)
    static constexpr StringLiteral test_bad_space4 =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123XM\0file1\0";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_bad_space4.begin(), test_bad_space4.end()));

    // Using 'Z' as an invalid action character
    static constexpr StringLiteral test_invalid_action =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 Z\0file1\0";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_invalid_action.begin(), test_invalid_action.end()));

    // Not a mode
    static constexpr StringLiteral test_not_mode_1 =
        ":100a44 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 A\0file1\0";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_not_mode_1.begin(), test_not_mode_1.end()));

    static constexpr StringLiteral test_not_mode_2 =
        ":100644 10a644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 A\0file1\0";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_not_mode_2.begin(), test_not_mode_2.end()));

    // Not a SHA
    static constexpr StringLiteral test_not_sha_1 =
        ":100644 100644 abcd123abcd123abcd12zabcd123abcd123 abcd123abcd123abcd123abcd123abcd123 A\0file1\0";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_not_sha_1.begin(), test_not_sha_1.end()));

    static constexpr StringLiteral test_not_sha_2 =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcdz23abcd123abcd123 A\0file1\0";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_not_sha_2.begin(), test_not_sha_2.end()));

    // Score with no terminator
    static constexpr StringLiteral test_missing_score_term =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 M50";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_missing_score_term.begin(), test_missing_score_term.end()));

    // Score is not a valid integer
    static constexpr StringLiteral test_invalid_score =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 M5x\0file1\0";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_invalid_score.begin(), test_invalid_score.end()));

    // Rename action with missing terminator after first file name
    static constexpr StringLiteral test_missing_file_term =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 R86\0file1";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_missing_file_term.begin(), test_missing_file_term.end()));

    // Copy action with missing terminator after first file name
    static constexpr StringLiteral test_missing_file_term2 =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 C68\0file1";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_missing_file_term2.begin(), test_missing_file_term2.end()));

    // Missing terminator after file name
    static constexpr StringLiteral test_missing_term =
        ":100644 100644 abcd123abcd123abcd123abcd123abcd123 abcd123abcd123abcd123abcd123abcd123 M\0file1";
    REQUIRE(!parse_git_diff_tree_line(test_out, test_missing_term.begin(), test_missing_term.end()));
}
