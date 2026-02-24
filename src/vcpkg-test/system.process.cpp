#include <vcpkg-test/util.h>

#include <vcpkg/base/system.process.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace vcpkg;

TEST_CASE ("captures-output", "[system.process]")
{
    auto test_program = Path(get_exe_path_of_current_process().parent_path()) / "reads-stdin";
    auto cmd = Command{test_program}.string_arg("this is printed when something is read");
    static constexpr std::size_t minimum_size = 1'000'000; // to exceed OS pipe buffer size
    constexpr StringLiteral example = "example";
    constexpr auto examples = (minimum_size / example.size()) + 1;
    constexpr auto input_size = examples * example.size();
    RedirectedProcessLaunchSettings settings;
    for (std::size_t idx = 0; idx < examples; ++idx)
    {
        settings.stdin_content.append(example.data(), example.size());
    }

    std::string expected;
    constexpr StringLiteral repeat = "this is printed when something is read";
    constexpr auto repeats = (input_size / 20) + (input_size % 20 != 0) + 1;
    for (std::size_t idx = 0; idx < repeats; ++idx)
    {
        expected.append(repeat.data(), repeat.size());
#if defined(_WIN32)
        expected.push_back('\r');
#endif // ^^^ _WIN32
        expected.push_back('\n');
    }

    expected.append("success");
#if defined(_WIN32)
    expected.push_back('\r');
#endif // ^^^ _WIN32
    expected.push_back('\n');

    auto run = cmd_execute_and_capture_output(cmd, settings).value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(run.exit_code == 0);
    REQUIRE(run.output == expected);
}

TEST_CASE ("closes-exit-minus-one cmd_execute", "[system.process]")
{
    auto test_program = Path(get_exe_path_of_current_process().parent_path()) / "closes-exit-minus-one";
    ProcessLaunchSettings settings;
    auto return_value = cmd_execute(Command{test_program}, settings).value_or_exit(VCPKG_LINE_INFO);
#ifdef _WIN32
    REQUIRE(return_value == 0xFFFFFFFFul);
#else  // ^^^ _WIN32 / !_WIN32 vvv
    if (WIFEXITED(return_value))
    {
        REQUIRE(WEXITSTATUS(return_value) == 0x000000FFul);
    }
    else
    {
        FAIL();
    }
#endif // ^^^ _WIN32
}

TEST_CASE ("closes-exit-minus-one cmd_execute_and_capture_output", "[system.process]")
{
    auto test_program = Path(get_exe_path_of_current_process().parent_path()) / "closes-exit-minus-one";
    RedirectedProcessLaunchSettings settings;
    settings.stdin_content = "this is some input that will be intentionally not read";
    auto run = cmd_execute_and_capture_output(Command{test_program}, settings).value_or_exit(VCPKG_LINE_INFO);
#ifdef _WIN32
    REQUIRE(run.exit_code == 0xFFFFFFFFul);
#else  // ^^^ _WIN32 / !_WIN32 vvv
    REQUIRE(run.exit_code == 0x000000FFul);
#endif // ^^^ _WIN32
}

TEST_CASE ("no closes-stdin crash", "[system.process]")
{
    auto test_program = Path(get_exe_path_of_current_process().parent_path()) / "closes-stdin";
    RedirectedProcessLaunchSettings settings;
    settings.stdin_content = "this is some input that will be intentionally not read";
    auto run = cmd_execute_and_capture_output(Command{test_program}, settings).value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(run.exit_code == 0);
    REQUIRE(run.output.empty());
}

TEST_CASE ("no closes-stdout crash", "[system.process]")
{
    auto test_program = Path(get_exe_path_of_current_process().parent_path()) / "closes-stdout";
    RedirectedProcessLaunchSettings settings;
    settings.stdin_content = "this is some input that will be intentionally not read";
    auto run = cmd_execute_and_capture_output(Command{test_program}, settings).value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(run.exit_code == 0);
    REQUIRE(run.output == "hello world");
}

TEST_CASE ("command try_append", "[system.process]")
{
    {
        Command a;
        REQUIRE(a.try_append(Command{"b"}));
        REQUIRE(a.command_line() == "b");
    }

    {
        Command a{"a"};
        REQUIRE(a.try_append(Command{}));
        REQUIRE(a.command_line() == "a");
    }

    {
        Command a{"a"};
        REQUIRE(a.try_append(Command{"b"}));
        REQUIRE(a.command_line() == "a b");
    }

    // size limits

    std::string one_string(1, 'a');
    std::string big_string(Command::maximum_allowed, 'a');
    std::string bigger_string(Command::maximum_allowed + 1, 'a');
    Command empty_cmd;
    Command one_cmd{one_string};
    Command big_cmd{big_string};
    Command bigger_cmd{bigger_string};

    REQUIRE(!bigger_cmd.try_append(empty_cmd));
    REQUIRE(bigger_cmd.command_line() == bigger_string);

    REQUIRE(big_cmd.try_append(empty_cmd));
    REQUIRE(big_cmd.command_line() == big_string);

    {
        auto cmd = empty_cmd;
        REQUIRE(!cmd.try_append(bigger_cmd));
        REQUIRE(cmd.empty());
        REQUIRE(cmd.try_append(big_cmd));
        REQUIRE(cmd.command_line() == big_string);
    }

    {
        auto cmd = one_cmd;
        REQUIRE(!cmd.try_append(big_cmd));
        REQUIRE(cmd.command_line() == one_string);
        // does not fit due to the needed space
        std::string almost_string(Command::maximum_allowed - 1, 'a');
        Command almost_cmd{almost_string};
        REQUIRE(!cmd.try_append(almost_cmd));
        REQUIRE(cmd.command_line() == one_string);
        // fits exactly
        std::string ok_string(Command::maximum_allowed - 2, 'a');
        Command ok_cmd{ok_string};
        REQUIRE(cmd.try_append(ok_cmd));
        auto expected = big_string;
        expected[1] = ' ';
        REQUIRE(cmd.command_line() == expected);
    }
}

TEST_CASE ("environment add_entry serializes as expected", "[system.process]")
{
    Environment env;
    env.add_entry("FOO", "alpha");
    env.add_entry("BAR", "value with spaces");

#if defined(_WIN32)
    // note that this is not one literal because embedded nulls would not compare correctly as the conversion to
    // std::wstring would stop at the first null.
    std::wstring expected;
    expected.append(L"FOO=alpha");
    expected.push_back(L'\0');
    expected.append(L"BAR=value with spaces");
    expected.push_back(L'\0');
    REQUIRE(env.get() == expected);
#else
    REQUIRE(env.get() == "FOO=alpha BAR=\"value with spaces\" ");
#endif
}

TEST_CASE ("environment remove_entry is case insensitive", "[system.process]")
{
    Environment env;
    env.add_entry("First", "1");
    env.add_entry("Second", "two words");
    env.add_entry("Third", "3");

    env.remove_entry("sEcOnD");

#if defined(_WIN32)
    std::wstring expected;
    expected.append(L"First=1");
    expected.push_back(L'\0');
    expected.append(L"Third=3");
    expected.push_back(L'\0');
    REQUIRE(env.get() == expected);
#else
    REQUIRE(env.get() == "First=1 Third=3 ");
#endif
}

TEST_CASE ("environment remove_entry of missing key is no-op", "[system.process]")
{
    Environment env;
    env.add_entry("One", "1");
    env.add_entry("Two", "2");
    const auto original = env.get();

    env.remove_entry("DoesNotExist");

    REQUIRE(env.get() == original);
}

TEST_CASE ("environment remove_entry handles first and last entries", "[system.process]")
{
    Environment env;
    env.add_entry("First", "one");
    env.add_entry("Middle", "two words");
    env.add_entry("Last", "three");

    env.remove_entry("FIRST");
#if defined(_WIN32)
    std::wstring expected_after_first;
    expected_after_first.append(L"Middle=two words");
    expected_after_first.push_back(L'\0');
    expected_after_first.append(L"Last=three");
    expected_after_first.push_back(L'\0');
    REQUIRE(env.get() == expected_after_first);
#else
    REQUIRE(env.get() == "Middle=\"two words\" Last=three ");
#endif

    env.remove_entry("last");
#if defined(_WIN32)
    std::wstring expected_after_last;
    expected_after_last.append(L"Middle=two words");
    expected_after_last.push_back(L'\0');
    REQUIRE(env.get() == expected_after_last);
#else
    REQUIRE(env.get() == "Middle=\"two words\" ");
#endif
}

TEST_CASE ("environment handles embedded quotes and slashes", "[system.process]")
{
    Environment env;
    env.add_entry("KEEP", "plain");
    env.add_entry("WEIRD", "C:/tool\\\"quoted\"/bin");
    env.add_entry("TAIL", "done");

#if defined(_WIN32)
    std::wstring expected;
    expected.append(L"KEEP=plain");
    expected.push_back(L'\0');
    expected.append(L"WEIRD=C:/tool\\\"quoted\"/bin");
    expected.push_back(L'\0');
    expected.append(L"TAIL=done");
    expected.push_back(L'\0');
    REQUIRE(env.get() == expected);
#else
    REQUIRE(env.get() == "KEEP=plain WEIRD=\"C:/tool\\\\\\\"quoted\\\"/bin\" TAIL=done ");
#endif

    env.remove_entry("wEiRd");

#if defined(_WIN32)
    std::wstring expected_without_weird;
    expected_without_weird.append(L"KEEP=plain");
    expected_without_weird.push_back(L'\0');
    expected_without_weird.append(L"TAIL=done");
    expected_without_weird.push_back(L'\0');
    REQUIRE(env.get() == expected_without_weird);
#else
    REQUIRE(env.get() == "KEEP=plain TAIL=done ");
#endif
}
