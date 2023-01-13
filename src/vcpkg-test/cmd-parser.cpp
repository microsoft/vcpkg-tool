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
