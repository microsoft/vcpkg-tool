#include <catch2/catch.hpp>

#include <vcpkg/base/cmd-parser.h>

using namespace vcpkg;

static std::vector<LocalizedString> localized(const std::vector<std::string>& strings)
{
    std::vector<LocalizedString> result;
    for (auto&& str : strings)
    {
        result.emplace_back(LocalizedString::from_raw(str));
    }

    return result;
}

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

TEST_CASE ("Arguments can be parsed as switches", "[cmd_parser]")
{
    std::vector<std::string> v;
    v.emplace_back("a");
    v.emplace_back("-b");
    v.emplace_back("--c");
    v.emplace_back("---d");
    std::vector<std::string> expected_remaining = v;
    v.emplace_back("--switch");
    v.emplace_back("--optional_switch");
    v.emplace_back("--optional_defaulted_switch");
    v.emplace_back("--duplicate");
    v.emplace_back("--duplicate");
    v.emplace_back("--duplicate");
    v.emplace_back("--duplicate");
    v.emplace_back("--no-disabled-switch");
    v.emplace_back("--no-opt-disabled-switch");
    v.emplace_back("--caSeySwitCh");
    v.emplace_back("--simple");
    CmdParser uut{v};

    bool unset_switch_value = true;
    CHECK(!uut.parse_switch("unset-switch", StabilityTag::Standard, unset_switch_value));
    CHECK(unset_switch_value);

    bool switch_value = false;
    CHECK(uut.parse_switch("switch", StabilityTag::Standard, switch_value));
    CHECK(switch_value);
    // parsing the same value again does not reparse
    CHECK(!uut.parse_switch("switch", StabilityTag::Standard, switch_value));
    CHECK(switch_value); // previous parsed value kept

    // Whether the optional is engaged upon encountering the switch must not change
    // to allow chains of optional-ness, such as when the default value is set by
    // an environment variable.
    Optional<bool> optional_switch_value;
    CHECK(!uut.parse_switch("unset-switch", StabilityTag::Standard, optional_switch_value));
    CHECK(!optional_switch_value.has_value());
    optional_switch_value.emplace(false);
    CHECK(!uut.parse_switch("unset-switch", StabilityTag::Standard, optional_switch_value));
    CHECK(optional_switch_value.value_or_exit(VCPKG_LINE_INFO) == false);
    optional_switch_value.clear();
    CHECK(uut.parse_switch("optional_switch", StabilityTag::Standard, optional_switch_value));
    CHECK(optional_switch_value.value_or_exit(VCPKG_LINE_INFO) == true);
    optional_switch_value.emplace(false);
    CHECK(uut.parse_switch("optional_defaulted_switch", StabilityTag::Standard, optional_switch_value));
    CHECK(optional_switch_value.value_or_exit(VCPKG_LINE_INFO) == true);

    // Duplicate switches emit errors and consume all duplicates
    bool duplicate_value = false;
    CHECK(uut.get_errors().empty());
    CHECK(uut.parse_switch("duplicate", StabilityTag::Standard, duplicate_value));
    CHECK(duplicate_value);
    CHECK(!uut.parse_switch("duplicate", StabilityTag::Standard, duplicate_value));
    CHECK(duplicate_value);

    // Switches can be explicitly disabled
    bool disabled_switch = true;
    CHECK(uut.parse_switch("disabled-switch", StabilityTag::Standard, disabled_switch));
    CHECK(!disabled_switch);

    Optional<bool> opt_disabled_switch;
    CHECK(uut.parse_switch("opt-disabled-switch", StabilityTag::Standard, opt_disabled_switch));
    CHECK(opt_disabled_switch.has_value());
    CHECK(opt_disabled_switch.value_or_exit(VCPKG_LINE_INFO) == false);

    // Switches are case insensitive
    bool casey_switch = false;
    CHECK(uut.parse_switch("caseyswitch", StabilityTag::Standard, casey_switch));
    CHECK(casey_switch);

    // Switches can use the simple return value form
    CHECK(uut.parse_switch("simple", StabilityTag::Standard));

    auto actual_remaining = uut.get_remaining_args();
    CHECK(expected_remaining == actual_remaining);
    CHECK(uut.get_errors() == localized({"error: the switch 'duplicate' was specified multiple times"}));
}

TEST_CASE ("Switches can have stability tags", "[cmd_parser]")
{
    std::vector<std::string> v;
    v.emplace_back("--a");
    v.emplace_back("--x-b");
    v.emplace_back("--z-c");
    v.emplace_back("--d");
    v.emplace_back("--x-e");
    v.emplace_back("--z-f");
    v.emplace_back("--g");
    v.emplace_back("--x-h");
    v.emplace_back("--z-i");
    CmdParser uut{v};

    bool unused;
    CHECK(uut.parse_switch("a", StabilityTag::Standard, unused));
    CHECK(uut.parse_switch("b", StabilityTag::Standard, unused));
    CHECK(!uut.parse_switch("c", StabilityTag::Standard, unused));

    CHECK(!uut.parse_switch("d", StabilityTag::Experimental, unused));
    CHECK(uut.parse_switch("e", StabilityTag::Experimental, unused));
    CHECK(!uut.parse_switch("f", StabilityTag::Experimental, unused));

    CHECK(!uut.parse_switch("g", StabilityTag::ImplementationDetail, unused));
    CHECK(!uut.parse_switch("h", StabilityTag::ImplementationDetail, unused));
    CHECK(uut.parse_switch("i", StabilityTag::ImplementationDetail, unused));
}

TEST_CASE ("Options can be parsed", "[cmd_parser]")
{
    std::vector<std::string> v;
    v.emplace_back("--equally-option=cantparsethis");
    v.emplace_back("--separate-option");
    v.emplace_back("separateparsethis");
    v.emplace_back("--x-evil-option");
    v.emplace_back("--evil-value");
    v.emplace_back("--optional-value=set");
    v.emplace_back("--optional-defaulted-value=set");
    v.emplace_back("--duplicate=a");
    v.emplace_back("--duplicate");
    v.emplace_back("b");
    v.emplace_back("--duplicate=last");
    CmdParser uut{v};

    std::string option_value;
    CHECK(uut.parse_option("equally-option", StabilityTag::Standard, option_value));
    CHECK(option_value == "cantparsethis");
    option_value = "kittens";
    CHECK(!uut.parse_option("equally-option", StabilityTag::Standard, option_value));
    CHECK(option_value == "kittens");

    CHECK(uut.parse_option("separate-option", StabilityTag::Standard, option_value));
    CHECK(option_value == "separateparsethis");
    option_value = "fluffy";
    CHECK(!uut.parse_option("separate-option", StabilityTag::Standard, option_value));
    CHECK(option_value == "fluffy");

    Optional<std::string> optional_value;
    // Trying to set the value of an option to a --dashed thing consumes the dashed thing but not the value
    CHECK(!uut.parse_option("evil-option", StabilityTag::Experimental, optional_value));
    CHECK(!optional_value.has_value());
    optional_value.clear();
    CHECK(!uut.parse_option("evil-option", StabilityTag::Experimental, optional_value));
    CHECK(!optional_value.has_value());

    Optional<std::string> optional_option_value;
    CHECK(!uut.parse_option("unset-option", StabilityTag::Standard, optional_option_value));
    CHECK(!optional_option_value.has_value());
    optional_option_value.emplace(std::string{"default value"});
    CHECK(!uut.parse_option("unset-option", StabilityTag::Standard, optional_option_value));
    CHECK(optional_option_value.value_or_exit(VCPKG_LINE_INFO) == "default value");
    optional_option_value.clear();
    CHECK(uut.parse_option("optional-value", StabilityTag::Standard, optional_option_value));
    CHECK(optional_option_value.value_or_exit(VCPKG_LINE_INFO) == "set");
    optional_option_value.emplace("default value");
    CHECK(uut.parse_option("optional-defaulted-value", StabilityTag::Standard, optional_option_value));
    CHECK(optional_option_value.value_or_exit(VCPKG_LINE_INFO) == "set");

    // Duplicate options emit errors, consume all duplicates, and take the last value
    std::string duplicate_value;
    auto expected_errors =
        localized({"error: the option 'evil-option' requires a value; if you intended to set 'evil-option' to "
                   "'--evil-value', use the equals form instead: --x-evil-option=--evil-value"});
    CHECK(uut.get_errors() == expected_errors);
    CHECK(uut.parse_option("duplicate", StabilityTag::Standard, duplicate_value));
    CHECK(duplicate_value == "last");
    expected_errors.push_back(LocalizedString::from_raw("error: the option 'duplicate' was specified multiple times"));
    CHECK(uut.get_errors() == expected_errors);
    duplicate_value = "good";
    CHECK(!uut.parse_option("duplicate", StabilityTag::Standard, duplicate_value));
    CHECK(duplicate_value == "good");

    CHECK(uut.get_errors() == expected_errors);
    CHECK(uut.get_remaining_args() == std::vector<std::string>{"--evil-value"});
}

TEST_CASE ("Options can have stability tags", "[cmd_parser]")
{
    std::vector<std::string> v;
    v.emplace_back("--a=v");
    v.emplace_back("--x-b=v");
    v.emplace_back("--z-c=v");
    v.emplace_back("--d=v");
    v.emplace_back("--x-e=v");
    v.emplace_back("--z-f=v");
    v.emplace_back("--g=v");
    v.emplace_back("--x-h=v");
    v.emplace_back("--z-i=v");
    CmdParser uut{v};

    std::string vtest = "bad";
    CHECK(uut.parse_option("a", StabilityTag::Standard, vtest));
    CHECK(vtest == "v");
    vtest = "bad";
    CHECK(uut.parse_option("b", StabilityTag::Standard, vtest));
    CHECK(vtest == "v");
    vtest = "good";
    CHECK(!uut.parse_option("c", StabilityTag::Standard, vtest));
    CHECK(vtest == "good");

    CHECK(!uut.parse_option("d", StabilityTag::Experimental, vtest));
    CHECK(vtest == "good");
    vtest = "bad";
    CHECK(uut.parse_option("e", StabilityTag::Experimental, vtest));
    CHECK(vtest == "v");
    vtest = "good";
    CHECK(!uut.parse_option("f", StabilityTag::Experimental, vtest));
    CHECK(vtest == "good");

    CHECK(!uut.parse_option("g", StabilityTag::ImplementationDetail, vtest));
    CHECK(vtest == "good");
    CHECK(!uut.parse_option("h", StabilityTag::ImplementationDetail, vtest));
    CHECK(vtest == "good");
    vtest = "bad";
    CHECK(uut.parse_option("i", StabilityTag::ImplementationDetail, vtest));
    CHECK(vtest == "v");

    CHECK(uut.get_errors().empty());
}

TEST_CASE ("Options missing values at the end generate errors", "[cmd_parser]")
{
    std::vector<std::string> v;
    v.emplace_back("--missing-value");
    CmdParser uut{v};
    std::string value;
    CHECK(!uut.parse_option("missing-value", StabilityTag::Standard, value));
    CHECK(value.empty());
    CHECK(uut.get_errors() == localized({"error: the option 'missing-value' requires a value"}));
    // The bad parameter is not consumed
    CHECK(uut.get_remaining_args() == std::vector<std::string>{"--missing-value"});
}

TEST_CASE ("Options missing values in the middle generate errors", "[cmd_parser]")
{
    std::vector<std::string> v;
    v.emplace_back("--missing-value");
    v.emplace_back("--switch");
    v.emplace_back("a");
    CmdParser uut{v};
    CHECK(uut.parse_switch("switch", StabilityTag::Standard));
    std::string value;
    CHECK(!uut.parse_option("missing-value", StabilityTag::Standard, value));
    CHECK(value.empty());
    CHECK(uut.get_errors() == localized({"error: the option 'missing-value' requires a value"}));
    // The bad parameter is not consumed
    CHECK(uut.get_remaining_args() == std::vector<std::string>{"--missing-value", "a"});
}

TEST_CASE ("Multi-options can be parsed", "[cmd_parser]")
{
    std::vector<std::string> v;
    v.emplace_back("--equally-option=cantparsethis");
    v.emplace_back("--separate-option");
    v.emplace_back("separateparsethis");
    v.emplace_back("--x-evil-option");
    v.emplace_back("--evil-value");
    v.emplace_back("--optional-value=set");
    v.emplace_back("--optional-value=set2");
    v.emplace_back("--optional-defaulted-value=set");
    v.emplace_back("--duplicate=a");
    v.emplace_back("--duplicate");
    v.emplace_back("b");
    v.emplace_back("--duplicate=last");
    CmdParser uut{v};

    std::vector<std::string> option_value;
    CHECK(uut.parse_multi_option("equally-option", StabilityTag::Standard, option_value));
    CHECK(option_value == std::vector<std::string>{"cantparsethis"});
    option_value[0] = "kittens";
    CHECK(!uut.parse_multi_option("equally-option", StabilityTag::Standard, option_value));
    CHECK(option_value == std::vector<std::string>{"kittens"});

    CHECK(uut.parse_multi_option("separate-option", StabilityTag::Standard, option_value));
    CHECK(option_value == std::vector<std::string>{"separateparsethis"});
    option_value[0] = "fluffy";
    CHECK(!uut.parse_multi_option("separate-option", StabilityTag::Standard, option_value));
    CHECK(option_value == std::vector<std::string>{"fluffy"});

    Optional<std::vector<std::string>> optional_value;
    CHECK(!uut.parse_multi_option("evil-option", StabilityTag::Experimental, optional_value));
    CHECK(!optional_value.has_value());
    optional_value.clear();
    CHECK(!uut.parse_multi_option("evil-option", StabilityTag::Experimental, optional_value));
    CHECK(!optional_value.has_value());

    Optional<std::vector<std::string>> optional_option_value;
    CHECK(!uut.parse_multi_option("unset-option", StabilityTag::Standard, optional_option_value));
    CHECK(!optional_option_value.has_value());
    optional_option_value.emplace(std::vector<std::string>{"default value"});
    CHECK(!uut.parse_multi_option("unset-option", StabilityTag::Standard, optional_option_value));
    CHECK(optional_option_value.value_or_exit(VCPKG_LINE_INFO) == std::vector<std::string>{"default value"});
    optional_option_value.clear();
    CHECK(uut.parse_multi_option("optional-value", StabilityTag::Standard, optional_option_value));
    CHECK(optional_option_value.value_or_exit(VCPKG_LINE_INFO) == std::vector<std::string>{"set", "set2"});
    optional_option_value.emplace(std::vector<std::string>{"default value"});
    CHECK(uut.parse_multi_option("optional-defaulted-value", StabilityTag::Standard, optional_option_value));
    CHECK(optional_option_value.value_or_exit(VCPKG_LINE_INFO) == std::vector<std::string>{"set"});

    std::vector<std::string> duplicate_value;
    const auto expected_errors =
        localized({"error: the option 'evil-option' requires a value; if you intended to set 'evil-option' to "
                   "'--evil-value', use the equals form instead: --x-evil-option=--evil-value"});
    CHECK(uut.get_errors() == expected_errors);
    CHECK(uut.parse_multi_option("duplicate", StabilityTag::Standard, duplicate_value));
    CHECK(duplicate_value == std::vector<std::string>{"a", "b", "last"});
    CHECK(uut.get_errors() == expected_errors);
    CHECK(uut.get_remaining_args() == std::vector<std::string>{"--evil-value"});
}

TEST_CASE ("Multi-options can have stability tags", "[cmd_parser]")
{
    std::vector<std::string> v;
    v.emplace_back("--a=v");
    v.emplace_back("--x-b=v");
    v.emplace_back("--z-c=v");
    v.emplace_back("--d=v");
    v.emplace_back("--x-e=v");
    v.emplace_back("--z-f=v");
    v.emplace_back("--g=v");
    v.emplace_back("--x-h=v");
    v.emplace_back("--z-i=v");
    CmdParser uut{v};

    std::vector<std::string> vtest{"bad"};
    CHECK(uut.parse_multi_option("a", StabilityTag::Standard, vtest));
    CHECK(vtest == std::vector<std::string>{"v"});
    vtest[0] = "bad";
    CHECK(uut.parse_multi_option("b", StabilityTag::Standard, vtest));
    CHECK(vtest == std::vector<std::string>{"v"});
    vtest[0] = "good";
    CHECK(!uut.parse_multi_option("c", StabilityTag::Standard, vtest));
    CHECK(vtest == std::vector<std::string>{"good"});

    CHECK(!uut.parse_multi_option("d", StabilityTag::Experimental, vtest));
    CHECK(vtest == std::vector<std::string>{"good"});
    vtest[0] = "bad";
    CHECK(uut.parse_multi_option("e", StabilityTag::Experimental, vtest));
    CHECK(vtest == std::vector<std::string>{"v"});
    vtest[0] = "good";
    CHECK(!uut.parse_multi_option("f", StabilityTag::Experimental, vtest));
    CHECK(vtest == std::vector<std::string>{"good"});

    CHECK(!uut.parse_multi_option("g", StabilityTag::ImplementationDetail, vtest));
    CHECK(vtest == std::vector<std::string>{"good"});
    CHECK(!uut.parse_multi_option("h", StabilityTag::ImplementationDetail, vtest));
    CHECK(vtest == std::vector<std::string>{"good"});
    vtest[0] = "bad";
    CHECK(uut.parse_multi_option("i", StabilityTag::ImplementationDetail, vtest));
    CHECK(vtest == std::vector<std::string>{"v"});

    CHECK(uut.get_errors().empty());
}

TEST_CASE ("Multi-options missing values at the end generate errors", "[cmd_parser]")
{
    std::vector<std::string> v;
    v.emplace_back("--missing-value");
    CmdParser uut{v};
    std::vector<std::string> value;
    CHECK(!uut.parse_multi_option("missing-value", StabilityTag::Standard, value));
    CHECK(value.empty());
    CHECK(uut.get_errors() == localized({"error: the option 'missing-value' requires a value"}));
    // The bad parameter is not consumed
    CHECK(uut.get_remaining_args() == std::vector<std::string>{"--missing-value"});
}

TEST_CASE ("Multi-options missing values in the middle generate errors", "[cmd_parser]")
{
    std::vector<std::string> v;
    v.emplace_back("--missing-value");
    v.emplace_back("--switch");
    v.emplace_back("a");
    CmdParser uut{v};
    CHECK(uut.parse_switch("switch", StabilityTag::Standard));
    std::vector<std::string> value;
    CHECK(!uut.parse_multi_option("missing-value", StabilityTag::Standard, value));
    CHECK(value.empty());
    CHECK(uut.get_errors() == localized({"error: the option 'missing-value' requires a value"}));
    // The bad parameter is not consumed
    CHECK(uut.get_remaining_args() == std::vector<std::string>{"--missing-value", "a"});
}

TEST_CASE ("Help table is generated", "[cmd_parser]")
{
    CmdParser uut{std::vector<std::string>{}};

    bool unused_bool;
    uut.parse_switch("a", StabilityTag::Standard, unused_bool, LocalizedString::from_raw("a help"));
    uut.parse_switch("b", StabilityTag::Experimental, unused_bool, LocalizedString::from_raw("b help"));

    Optional<bool> unused_optional_bool;
    uut.parse_switch("c", StabilityTag::Standard, unused_optional_bool, LocalizedString::from_raw("c help"));
    uut.parse_switch("d", StabilityTag::Experimental, unused_optional_bool, LocalizedString::from_raw("d help"));

    uut.parse_switch("e", StabilityTag::Standard, LocalizedString::from_raw("e help"));
    uut.parse_switch("f", StabilityTag::Experimental, LocalizedString::from_raw("f help"));

    std::string unused_option;
    uut.parse_option("g", StabilityTag::Standard, unused_option, LocalizedString::from_raw("g help"));
    uut.parse_option("h", StabilityTag::Experimental, unused_option, LocalizedString::from_raw("h help"));

    Optional<std::string> unused_optional_option;
    uut.parse_option("i", StabilityTag::Standard, unused_optional_option, LocalizedString::from_raw("i help"));
    uut.parse_option("j", StabilityTag::Experimental, unused_optional_option, LocalizedString::from_raw("j help"));

    Optional<std::string> unused_unique_option;
    uut.parse_option("k", StabilityTag::Standard, unused_unique_option, LocalizedString::from_raw("k help"));
    uut.parse_option("l", StabilityTag::Experimental, unused_unique_option, LocalizedString::from_raw("l help"));

    std::vector<std::string> unused_multi_option;
    uut.parse_multi_option("m", StabilityTag::Standard, unused_multi_option, LocalizedString::from_raw("m help"));
    uut.parse_multi_option("n", StabilityTag::Experimental, unused_multi_option, LocalizedString::from_raw("n help"));

    Optional<std::vector<std::string>> unused_optional_multi_option;
    uut.parse_multi_option(
        "o", StabilityTag::Standard, unused_optional_multi_option, LocalizedString::from_raw("m help"));
    uut.parse_multi_option(
        "p", StabilityTag::Experimental, unused_optional_multi_option, LocalizedString::from_raw("n help"));

    const auto expected = LocalizedString::from_raw(
        R"(Options:
  --a                             a help
  --x-b                           b help
  --c                             c help
  --x-d                           d help
  --e                             e help
  --x-f                           f help
  --g=...                         g help
  --x-h=...                       h help
  --i=...                         i help
  --x-j=...                       j help
  --k=...                         k help
  --x-l=...                       l help
  --m=...                         m help
  --x-n=...                       n help
  --o=...                         m help
  --x-p=...                       n help
)");
    CHECK(uut.get_options_table() == expected);
}

TEST_CASE ("Enforce zero remaining args", "[cmd_parser]")
{
    {
        CmdParser uut{std::vector<std::string>{}};
        uut.enforce_no_remaining_args("example");
        CHECK(uut.get_errors().empty());
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"extra"}};
        uut.enforce_no_remaining_args("example");
        CHECK(uut.get_errors() == localized({"error: the command 'example' does not accept any additional arguments",
                                             "error: unexpected argument: extra"}));
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"extra", "extra2"}};
        uut.enforce_no_remaining_args("example");
        CHECK(uut.get_errors() == localized({"error: the command 'example' does not accept any additional arguments",
                                             "error: unexpected argument: extra",
                                             "error: unexpected argument: extra2"}));
        CHECK(uut.get_remaining_args().empty());
    }
}

TEST_CASE ("Consume only remaining arg", "[cmd_parser]")
{
    {
        CmdParser uut{std::vector<std::string>{}};
        CHECK(uut.consume_only_remaining_arg("example").empty());
        CHECK(uut.get_errors() == localized({"error: the command 'example' requires exactly one argument"}));
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg"}};
        CHECK(uut.consume_only_remaining_arg("example") == "first-arg");
        CHECK(uut.get_errors().empty());
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg", "second-arg"}};
        CHECK(uut.consume_only_remaining_arg("example").empty());
        CHECK(uut.get_errors() == localized({"error: the command 'example' requires exactly one argument",
                                             "error: unexpected argument: second-arg"}));
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg", "second-arg", "third-arg"}};
        CHECK(uut.consume_only_remaining_arg("example").empty());
        CHECK(uut.get_errors() == localized({"error: the command 'example' requires exactly one argument",
                                             "error: unexpected argument: second-arg",
                                             "error: unexpected argument: third-arg"}));
        CHECK(uut.get_remaining_args().empty());
    }
}

TEST_CASE ("Consume zero or one remaining args", "[cmd_parser]")
{
    {
        CmdParser uut{std::vector<std::string>{}};
        CHECK(!uut.consume_only_remaining_arg_optional("example").has_value());
        CHECK(uut.get_errors().empty());
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg"}};
        CHECK(uut.consume_only_remaining_arg_optional("example").value_or_exit(VCPKG_LINE_INFO) == "first-arg");
        CHECK(uut.get_errors().empty());
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg", "second-arg"}};
        CHECK(!uut.consume_only_remaining_arg_optional("example").has_value());
        CHECK(uut.get_errors() == localized({"error: the command 'example' requires zero or one arguments",
                                             "error: unexpected argument: second-arg"}));
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg", "second-arg", "third-arg"}};
        CHECK(!uut.consume_only_remaining_arg_optional("example").has_value());
        CHECK(uut.get_errors() == localized({"error: the command 'example' requires zero or one arguments",
                                             "error: unexpected argument: second-arg",
                                             "error: unexpected argument: third-arg"}));
        CHECK(uut.get_remaining_args().empty());
    }
}

TEST_CASE ("Consume remaining args", "[cmd_parser]")
{
    {
        CmdParser uut{std::vector<std::string>{}};
        CHECK(uut.consume_remaining_args().empty());
        CHECK(uut.get_errors().empty());
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg"}};
        CHECK(uut.consume_remaining_args() == std::vector<std::string>{"first-arg"});
        CHECK(uut.get_errors().empty());
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg", "second-arg"}};
        CHECK(uut.consume_remaining_args() == std::vector<std::string>{"first-arg", "second-arg"});
        CHECK(uut.get_errors().empty());
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{}};
        CHECK(uut.consume_remaining_args("example", 3).empty());
        CHECK(uut.get_errors() ==
              localized({"error: the command 'example' requires exactly 3 arguments, but 0 were provided"}));
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg", "second-arg", "third-arg"}};
        CHECK(uut.consume_remaining_args("example", 3) ==
              std::vector<std::string>{"first-arg", "second-arg", "third-arg"});
        CHECK(uut.get_errors().empty());
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg", "second-arg", "third-arg", "fourth-arg"}};
        CHECK(uut.consume_remaining_args("example", 3).empty());
        CHECK(uut.get_errors() ==
              localized({"error: the command 'example' requires exactly 3 arguments, but 4 were provided",
                         "error: unexpected argument: fourth-arg"}));
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{}};
        CHECK(uut.consume_remaining_args("example", 2, 3).empty());
        CHECK(uut.get_errors() ==
              localized(
                  {"error: the command 'example' requires between 2 and 3 arguments, inclusive, but 0 were provided"}));
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg"}};
        CHECK(uut.consume_remaining_args("example", 2, 3).empty());
        CHECK(uut.get_errors() ==
              localized(
                  {"error: the command 'example' requires between 2 and 3 arguments, inclusive, but 1 were provided"}));
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg", "second-arg"}};
        CHECK(uut.consume_remaining_args("example", 2, 3) == std::vector<std::string>{"first-arg", "second-arg"});
        CHECK(uut.get_errors().empty());
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg", "second-arg", "third-arg"}};
        CHECK(uut.consume_remaining_args("example", 2, 3) ==
              std::vector<std::string>{"first-arg", "second-arg", "third-arg"});
        CHECK(uut.get_errors().empty());
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"first-arg", "second-arg", "third-arg", "fourth-arg"}};
        CHECK(uut.consume_remaining_args("example", 2, 3).empty());
        CHECK(uut.get_errors() ==
              localized(
                  {"error: the command 'example' requires between 2 and 3 arguments, inclusive, but 4 were provided",
                   "error: unexpected argument: fourth-arg"}));
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"--first-arg"}};
        CHECK(uut.consume_remaining_args() == std::vector<std::string>{});
        const auto expected_errors = localized({"error: unexpected option: --first-arg"});
        CHECK(uut.get_errors() == expected_errors);
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"--first-arg"}};
        CHECK(uut.consume_only_remaining_arg("command") == std::string{});
        const auto expected_errors = localized({"error: unexpected option: --first-arg"});
        CHECK(uut.get_errors() == expected_errors);
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"--first-arg"}};
        CHECK(!uut.consume_only_remaining_arg_optional("command").has_value());
        const auto expected_errors = localized({"error: unexpected option: --first-arg"});
        CHECK(uut.get_errors() == expected_errors);
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"--first-arg", "second-arg"}};
        CHECK(uut.consume_remaining_args("command", 2) == std::vector<std::string>{});
        const auto expected_errors = localized({"error: unexpected option: --first-arg"});
        CHECK(uut.get_errors() == expected_errors);
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"--first-arg", "second-arg"}};
        // note that arity isn't checked if the 'looks like switch' check fails
        CHECK(uut.consume_remaining_args("command", 3) == std::vector<std::string>{});
        const auto expected_errors = localized({"error: unexpected option: --first-arg"});
        CHECK(uut.get_errors() == expected_errors);
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"--first-arg", "second-arg"}};
        CHECK(uut.consume_remaining_args("command", 1, 2) == std::vector<std::string>{});
        const auto expected_errors = localized({"error: unexpected option: --first-arg"});
        CHECK(uut.get_errors() == expected_errors);
        CHECK(uut.get_remaining_args().empty());
    }

    {
        CmdParser uut{std::vector<std::string>{"--first-arg", "second-arg"}};
        // note that arity isn't checked if the 'looks like switch' check fails
        CHECK(uut.consume_remaining_args("command", 3, 4) == std::vector<std::string>{});
        const auto expected_errors = localized({"error: unexpected option: --first-arg"});
        CHECK(uut.get_errors() == expected_errors);
        CHECK(uut.get_remaining_args().empty());
    }
}

TEST_CASE ("delistify_conjoined_value", "[cmd_parser]")
{
    {
        std::vector<std::string> empty;
        delistify_conjoined_multivalue(empty);
        CHECK(empty.empty());
    }

    {
        std::vector<std::string> only_one{"a"};
        delistify_conjoined_multivalue(only_one);
        CHECK(only_one == std::vector<std::string>{"a"});
    }

    {
        std::vector<std::string> several{"a", "b", "c"};
        delistify_conjoined_multivalue(several);
        CHECK(several == std::vector<std::string>{"a", "b", "c"});
    }

    {
        std::vector<std::string> uut{"a", ",,,,,", "c"};
        delistify_conjoined_multivalue(uut);
        CHECK(uut == std::vector<std::string>{"a", "c"});
    }

    {
        std::vector<std::string> uut{"a", ",,,,,b", "c"};
        delistify_conjoined_multivalue(uut);
        CHECK(uut == std::vector<std::string>{"a", "b", "c"});
    }

    {
        std::vector<std::string> uut{"a", ",,,,,b,d,", "c"};
        delistify_conjoined_multivalue(uut);
        CHECK(uut == std::vector<std::string>{"a", "b", "d", "c"});
    }

    {
        std::vector<std::string> uut{"a,b", "c,d"};
        delistify_conjoined_multivalue(uut);
        CHECK(uut == std::vector<std::string>{"a", "b", "c", "d"});
    }
}
