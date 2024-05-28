#include <vcpkg-test/util.h>

#include <vcpkg/base/system.process.h>

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
