#include <vcpkg-test/util.h>

#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.process.h>

using namespace vcpkg;

TEST_CASE ("captures-output", "[system.process]")
{
    auto test_program = Path(get_exe_path_of_current_process().parent_path()) / "reads-stdin";
    Command cmd{test_program};
    cmd.string_arg("this is printed when something is read");
    static constexpr std::size_t minimum_size = 1'000'000; // to exceed OS pipe buffer size
    constexpr StringLiteral example = "example";
    constexpr auto examples = (minimum_size / example.size()) + 1;
    std::string input;
    constexpr auto input_size = examples * example.size();
    for (std::size_t idx = 0; idx < examples; ++idx)
    {
        input.append(example.data(), example.size());
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
    auto run = cmd_execute_and_capture_output(
                   cmd, default_working_directory, default_environment, Encoding::Utf8, EchoInDebug::Hide, input)
                   .value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(run.exit_code == 0);
    REQUIRE(run.output == expected);
}

TEST_CASE ("no closes-stdin crash", "[system.process]")
{
    auto test_program = Path(get_exe_path_of_current_process().parent_path()) / "closes-stdin";
    Command cmd{test_program};
    auto run = cmd_execute_and_capture_output(cmd,
                                              default_working_directory,
                                              default_environment,
                                              Encoding::Utf8,
                                              EchoInDebug::Hide,
                                              "this is some input that will be intentionally not read")
                   .value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(run.exit_code == 0);
    REQUIRE(run.output.empty());
}

TEST_CASE ("no closes-stdout crash", "[system.process]")
{
    auto test_program = Path(get_exe_path_of_current_process().parent_path()) / "closes-stdout";
    Command cmd{test_program};
    auto run = cmd_execute_and_capture_output(cmd,
                                              default_working_directory,
                                              default_environment,
                                              Encoding::Utf8,
                                              EchoInDebug::Hide,
                                              "this is some input that will be read")
                   .value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(run.exit_code == 0);
    REQUIRE(run.output == "hello world");
}
