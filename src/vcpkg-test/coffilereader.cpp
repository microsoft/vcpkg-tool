#include <catch2/catch.hpp>

#include <vcpkg/base/cofffilereader.h>

using namespace vcpkg;

TEST_CASE ("tokenize-command-line", "[cofffilereader]")
{
    using Vec = std::vector<std::string>;
    CHECK(tokenize_command_line("") == Vec{});
    CHECK(tokenize_command_line("a b c") == Vec{"a", "b", "c"});
    CHECK(tokenize_command_line("a b c ") == Vec{"a", "b", "c"});
    CHECK(tokenize_command_line(" a b c ") == Vec{"a", "b", "c"});
    CHECK(tokenize_command_line(" a b c") == Vec{"a", "b", "c"});
    CHECK(tokenize_command_line("a\"embedded quotes\"") == Vec{"aembedded quotes"});
    CHECK(tokenize_command_line("a\\slash\\b") == Vec{"a\\slash\\b"});
    // n backslashes not followed by a quotation mark produce n backslashes
    CHECK(tokenize_command_line("a\\\\\\slash\\b") == Vec{"a\\\\\\slash\\b"});
    CHECK(tokenize_command_line("an arg with \\\"quotes") == Vec{"an", "arg", "with", "\"quotes"});
    CHECK(tokenize_command_line("an arg with \"\\\"quotes\"") == Vec{"an", "arg", "with", "\"quotes"});
    CHECK(tokenize_command_line("arg \"quoted\" suffix") == Vec{"arg", "quoted", "suffix"});
    // 2n + 1 backslashes followed by a quotation mark produce n backslashes followed by a (escaped) quotation mark
    CHECK(tokenize_command_line("arg \"quoted\\\" suffix") == Vec{"arg", "quoted\" suffix"});
    // 2n backslashes followed by a quotation mark produce n backslashes followed by a (terminal) quotation mark
    CHECK(tokenize_command_line("arg \"quoted\\\\\" suffix") == Vec{"arg", "quoted\\", "suffix"});
    CHECK(tokenize_command_line("arg \"quoted\\\\\\\" suffix") == Vec{"arg", "quoted\\\" suffix"});
    CHECK(tokenize_command_line("arg \"quoted\\\\\\\\\" suffix") == Vec{"arg", "quoted\\\\", "suffix"});
    // The above cases but at the end
    CHECK(tokenize_command_line("\\") == Vec{"\\"});
    CHECK(tokenize_command_line("\\\\") == Vec{"\\\\"});
    CHECK(tokenize_command_line("\\\\\\") == Vec{"\\\\\\"});
    CHECK(tokenize_command_line("arg \"quoted\\\"") == Vec{"arg", "quoted\""});
    CHECK(tokenize_command_line("arg \"quoted\\\\\"") == Vec{"arg", "quoted\\"});
    CHECK(tokenize_command_line("arg \"quoted\\\\\\\"") == Vec{"arg", "quoted\\\""});
    CHECK(tokenize_command_line("arg \"quoted\\\\\\\\\"") == Vec{"arg", "quoted\\\\"});
}
