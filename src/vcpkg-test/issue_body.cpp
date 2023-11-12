#include <vcpkg-test/util.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/commands.build.h>

namespace
{
    const std::string file_content = R"(00 32 byte long line xxxxxxxxxx
01 32 byte long line xxxxxxxxxx
02 32 byte long line xxxxxxxxxx
03 32 byte long line xxxxxxxxxx
04 32 byte long line xxxxxxxxxx
05 32 byte long line xxxxxxxxxx
06 32 byte long line xxxxxxxxxx)";

    const auto expected_body = R"(<details><summary>test 2</summary>

```
00 32 byte long line xxxxxxxxxx
...
Skipped 4 lines
...
05 32 byte long line xxxxxxxxxx
06 32 byte long line xxxxxxxxxx
```
</details>

)";

    const auto block_prefix = "<details><summary>test</summary>\n\n```\n";
    const auto block_postfix = "\n```\n</details>\n\n";
}

TEST_CASE ("Testing append_log", "[github-issue-body]")
{
    using namespace vcpkg;
    {
        std::string out;
        append_log("test", file_content, 100, out);
        CHECK(out == ""); // Not enough space at all
        out.clear();
        append_log("test 2", file_content, file_content.size(), out);
        CHECK(out == expected_body); // Not enough space
        out.clear();
        append_log("test", file_content, static_cast<int>(file_content.size() + 100), out);
        CHECK(out == block_prefix + file_content + block_postfix); // Enough space
    }
}

TEST_CASE ("Testing append_log extra_size", "[github-issue-body]")
{
    using namespace vcpkg;
    {
        std::string out;
        std::vector<std::pair<Path, std::string>> logs{
            {"not_included_1", file_content}, {"test", file_content}, {"test 2", file_content}};
        append_logs(std::move(logs), 500, out);
        CHECK(out == block_prefix + file_content + block_postfix + expected_body);
    }
    {
        using namespace std::string_literals;
        std::string out;
        std::vector<std::pair<Path, std::string>> logs{{"test", file_content}, {"test", "smal"}, {"test", "smal"}};
        // Enough space to output all logs, but if we would output the largest log first, there would be not enough
        // space for this log if every log get the size total_size/number_of_logs.
        append_logs(std::move(logs), file_content.size() + 3 * 110, out);
        CHECK(out == fmt::format("{0}smal{1}{0}smal{1}{0}{2}{1}", block_prefix, block_postfix, file_content));
    }
}
