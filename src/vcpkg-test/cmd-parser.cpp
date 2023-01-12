#include <catch2/catch.hpp>

#include <vcpkg/base/cmd-parser.h>

using namespace vcpkg;

TEST_CASE ("Smoke test help table formatter", "[cmd_parser]")
{
    HelpTableFormatter uut;

    uut.header("This is a header");
    uut.format("short-arg", "short help text");
    uut.format("a-really-long-arg-that-does-not-fit-in-the-first-column-and-keeps-going", "shorty");
    uut.format("short-arg",
               "some really long help text that does not fit on the same line because we have a 100 character line "
               "limit and oh god it keeps going and going");
    uut.format("a-really-long-arg-combined-with-some-really-long-help-text",
               "another instance of that really long help text goes here to demonstrate that the worst case combo can "
               "be accommodated");

    uut.blank();
    uut.example("some example command");
    uut.text("this is some text");

    const char* const expected = R"(This is a header:
  short-arg                       short help text
  a-really-long-arg-that-does-not-fit-in-the-first-column-and-keeps-going
                                  shorty
  short-arg                       some really long help text that does not fit on the same line
                                  because we have a 100 character line limit and oh god it keeps
                                  going and going
  a-really-long-arg-combined-with-some-really-long-help-text
                                  another instance of that really long help text goes here to
                                  demonstrate that the worst case combo can be accommodated

some example command
this is some text)";

    CHECK(uut.m_str == expected);
}

TEST_CASE ("Arguments can be converted from argc/argv", "[cmd_parser]")
{
#if defined(_WIN32)
    const wchar_t* argv[] = {L"program.exe", L"a", L"b"};
#else
    const char* argv[] = {"a.out", "a", "b"};
#endif // ^^^ !_WIN32
    CHECK(convert_argc_argv_to_arguments(3, argv) == std::vector<std::string>{"a", "b"});
}

namespace
{
    struct NeverReadLines : ILineReader
    {
        ExpectedL<std::vector<std::string>> read_lines(const Path& file_path) const override
        {
            FAIL();
            (void)file_path;
            return std::vector<std::string>{};
        }
    };

    struct FakeReadLines : ILineReader
    {
        std::vector<std::string> answer;

        FakeReadLines() = default;
        explicit FakeReadLines(const std::vector<std::string>& answer_) : answer(answer_) { }

        ExpectedL<std::vector<std::string>> read_lines(const Path& file_path) const override
        {
            CHECK(file_path == "filename");
            return answer;
        }
    };
}

TEST_CASE ("Response file parameters can be processed", "[cmd_parser]")
{
    {
        std::vector<std::string> empty;
        replace_response_file_parameters(empty, NeverReadLines{}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(empty == std::vector<std::string>{});
    }

    {
        std::vector<std::string> no_responses{"a", "b", "c"};
        replace_response_file_parameters(no_responses, NeverReadLines{}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(no_responses == std::vector<std::string>{"a", "b", "c"});
    }

    {
        std::vector<std::string> remove_only{"@filename"};
        replace_response_file_parameters(remove_only, FakeReadLines{}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(remove_only == std::vector<std::string>{});
    }

    {
        std::vector<std::string> remove_first{"@filename", "a", "b"};
        replace_response_file_parameters(remove_first, FakeReadLines{}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(remove_first == std::vector<std::string>{"a", "b"});
    }

    {
        std::vector<std::string> remove_middle{"a", "@filename", "b"};
        replace_response_file_parameters(remove_middle, FakeReadLines{}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(remove_middle == std::vector<std::string>{"a", "b"});
    }

    {
        std::vector<std::string> remove_last{"a", "b", "@filename"};
        replace_response_file_parameters(remove_last, FakeReadLines{}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(remove_last == std::vector<std::string>{"a", "b"});
    }

    const std::vector<std::string> only_x{"x"};
    {
        std::vector<std::string> insert_only{"@filename"};
        replace_response_file_parameters(insert_only, FakeReadLines{only_x}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(insert_only == std::vector<std::string>{"x"});
    }

    {
        std::vector<std::string> insert_first{"@filename", "a", "b"};
        replace_response_file_parameters(insert_first, FakeReadLines{only_x}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(insert_first == std::vector<std::string>{"x", "a", "b"});
    }

    {
        std::vector<std::string> insert_middle{"a", "@filename", "b"};
        replace_response_file_parameters(insert_middle, FakeReadLines{only_x}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(insert_middle == std::vector<std::string>{"a", "x", "b"});
    }

    {
        std::vector<std::string> insert_last{"a", "b", "@filename"};
        replace_response_file_parameters(insert_last, FakeReadLines{only_x}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(insert_last == std::vector<std::string>{"a", "b", "x"});
    }

    const std::vector<std::string> xy{"x", "y"};
    {
        std::vector<std::string> multi_insert_only{"@filename"};
        replace_response_file_parameters(multi_insert_only, FakeReadLines{xy}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(multi_insert_only == std::vector<std::string>{"x", "y"});
    }

    {
        std::vector<std::string> multi_insert_first{"@filename", "a", "b"};
        replace_response_file_parameters(multi_insert_first, FakeReadLines{xy}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(multi_insert_first == std::vector<std::string>{"x", "y", "a", "b"});
    }

    {
        std::vector<std::string> multi_insert_middle{"a", "@filename", "b"};
        replace_response_file_parameters(multi_insert_middle, FakeReadLines{xy}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(multi_insert_middle == std::vector<std::string>{"a", "x", "y", "b"});
    }

    {
        std::vector<std::string> multi_insert_last{"a", "b", "@filename"};
        replace_response_file_parameters(multi_insert_last, FakeReadLines{xy}).value_or_exit(VCPKG_LINE_INFO);
        CHECK(multi_insert_last == std::vector<std::string>{"a", "b", "x", "y"});
    }
}
