#include <vcpkg/base/system-headers.h>

#include <catch2/catch.hpp>

#include <vcpkg/base/optional.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>

#include <string>

using vcpkg::CPUArchitecture;
using vcpkg::get_environment_variable;
using vcpkg::guess_visual_studio_prompt_target_architecture;
using vcpkg::nullopt;
using vcpkg::Optional;
using vcpkg::set_environment_variable;
using vcpkg::StringView;
using vcpkg::to_cpu_architecture;
using vcpkg::ZStringView;

namespace
{
    struct environment_variable_resetter
    {
        explicit environment_variable_resetter(ZStringView varname_)
            : varname(varname_), old_value(get_environment_variable(varname))
        {
        }

        ~environment_variable_resetter() { set_environment_variable(varname, old_value); }

        environment_variable_resetter(const environment_variable_resetter&) = delete;
        environment_variable_resetter& operator=(const environment_variable_resetter&) = delete;

    private:
        ZStringView varname;
        Optional<std::string> old_value;
    };
}

TEST_CASE ("[to_cpu_architecture]", "system")
{
    struct test_case
    {
        Optional<CPUArchitecture> expected;
        StringView input;
    };

    const test_case test_cases[] = {
        {CPUArchitecture::X86, "x86"},
        {CPUArchitecture::X86, "X86"},
        {CPUArchitecture::X64, "x64"},
        {CPUArchitecture::X64, "X64"},
        {CPUArchitecture::X64, "AmD64"},
        {CPUArchitecture::ARM, "ARM"},
        {CPUArchitecture::ARM64, "ARM64"},
        {nullopt, "ARM6"},
        {nullopt, "AR"},
        {nullopt, "Intel"},
        {nullopt, "%processor_architew6432%"},
    };

    for (auto&& instance : test_cases)
    {
        CHECK(to_cpu_architecture(instance.input) == instance.expected);
    }
}

TEST_CASE ("from_cpu_architecture", "[system]")
{
    struct test_case
    {
        CPUArchitecture input;
        ZStringView expected;
    };

    const test_case test_cases[] = {
        {CPUArchitecture::X86, "x86"},
        {CPUArchitecture::X64, "x64"},
        {CPUArchitecture::ARM, "arm"},
        {CPUArchitecture::ARM64, "arm64"},
    };

    for (auto&& instance : test_cases)
    {
        CHECK(to_zstring_view(instance.input) == instance.expected);
    }
}

TEST_CASE ("guess_visual_studio_prompt", "[system]")
{
    environment_variable_resetter reset_VSCMD_ARG_TGT_ARCH{"VSCMD_ARG_TGT_ARCH"};
    environment_variable_resetter reset_VCINSTALLDIR{"VCINSTALLDIR"};
    environment_variable_resetter reset_Platform{"Platform"};

    set_environment_variable("Platform", "x86"); // ignored if VCINSTALLDIR unset
    set_environment_variable("VCINSTALLDIR", nullopt);
    set_environment_variable("VSCMD_ARG_TGT_ARCH", nullopt);
    CHECK(!guess_visual_studio_prompt_target_architecture().has_value());
    set_environment_variable("VSCMD_ARG_TGT_ARCH", "x86");
    CHECK(guess_visual_studio_prompt_target_architecture().value_or_exit(VCPKG_LINE_INFO) == CPUArchitecture::X86);
    set_environment_variable("VSCMD_ARG_TGT_ARCH", "x64");
    CHECK(guess_visual_studio_prompt_target_architecture().value_or_exit(VCPKG_LINE_INFO) == CPUArchitecture::X64);
    set_environment_variable("VSCMD_ARG_TGT_ARCH", "arm");
    CHECK(guess_visual_studio_prompt_target_architecture().value_or_exit(VCPKG_LINE_INFO) == CPUArchitecture::ARM);
    set_environment_variable("VSCMD_ARG_TGT_ARCH", "arm64");
    CHECK(guess_visual_studio_prompt_target_architecture().value_or_exit(VCPKG_LINE_INFO) == CPUArchitecture::ARM64);

    // check that apparent "nested" prompts defer to "vsdevcmd"
    set_environment_variable("VCINSTALLDIR", "anything");
    CHECK(guess_visual_studio_prompt_target_architecture().value_or_exit(VCPKG_LINE_INFO) == CPUArchitecture::ARM64);
    set_environment_variable("VSCMD_ARG_TGT_ARCH", nullopt);
    set_environment_variable("Platform", nullopt);
    CHECK(guess_visual_studio_prompt_target_architecture().value_or_exit(VCPKG_LINE_INFO) == CPUArchitecture::X86);
    set_environment_variable("Platform", "x86");
    CHECK(guess_visual_studio_prompt_target_architecture().value_or_exit(VCPKG_LINE_INFO) == CPUArchitecture::X86);
    set_environment_variable("Platform", "x64");
    CHECK(guess_visual_studio_prompt_target_architecture().value_or_exit(VCPKG_LINE_INFO) == CPUArchitecture::X64);
}

TEST_CASE ("cmdlinebuilder", "[system]")
{
    using vcpkg::Command;

    Command cmd;
    cmd.string_arg("relative/path.exe");
    cmd.string_arg("abc");
    cmd.string_arg("hello world!");
    cmd.string_arg("|");
    cmd.string_arg(";");
    REQUIRE(cmd.command_line() == "relative/path.exe abc \"hello world!\" \"|\" \";\"");

    cmd.clear();

    cmd.string_arg("trailing\\slash\\");
    cmd.string_arg("inner\"quotes");
#ifdef _WIN32
    REQUIRE(cmd.command_line() == "\"trailing\\slash\\\\\" \"inner\\\"quotes\"");
#else
    REQUIRE(cmd.command_line() == "\"trailing\\\\slash\\\\\" \"inner\\\"quotes\"");
#endif
}

TEST_CASE ("cmd_execute_and_capture_output_parallel", "[system]")
{
    std::vector<vcpkg::Command> vec;
    for (size_t i = 0; i < 50; ++i)
    {
#if defined(_WIN32)
        vcpkg::Command cmd("cmd.exe");
        cmd.string_arg("/c");
        cmd.string_arg(fmt::format("echo {}", i));
#else
        vcpkg::Command cmd("echo");
        const auto cmd_str = std::string(i, 'a');
        cmd.string_arg(cmd_str);
#endif
        vec.emplace_back(std::move(cmd));
    }

    auto res = vcpkg::cmd_execute_and_capture_output_parallel(vcpkg::View<vcpkg::Command>(vec));

    for (size_t i = 0; i != res.size(); ++i)
    {
        auto out = res[i].get();
        REQUIRE(out != nullptr);
        REQUIRE(out->exit_code == 0);

#if defined(_WIN32)
        REQUIRE(out->output == (fmt::format("{}\r\n", i)));
#else
        REQUIRE(out->output == (std::string(i, 'a') + "\n"));
#endif
    }
}

TEST_CASE ("append_shell_escaped", "[system]")
{
    using vcpkg::Command;

    Command cmd;

    cmd.clear();
    cmd.string_arg("shell_escaped_chars1");
    cmd.string_arg(",");
    cmd.string_arg(";");
    cmd.string_arg("&");
    cmd.string_arg("^");
    cmd.string_arg("|");
    cmd.string_arg("(");
    cmd.string_arg(")");
    cmd.string_arg("'");
    REQUIRE(cmd.command_line() == "shell_escaped_chars1 \",\" \";\" \"&\" \"^\" \"|\" \"(\" \")\" \"'\"");

    cmd.clear();
    // double-quote and backslash must be escaped on all platforms
    cmd.string_arg("shell_escaped_chars2");
    cmd.string_arg("\"");
    cmd.string_arg("\\");
    REQUIRE(cmd.command_line() == "shell_escaped_chars2 \"\\\"\" \"\\\\\"");

    cmd.clear();
    // backquote and dollar-sign must be escaped on non-_WIN32 platforms
    cmd.string_arg("shell_escaped_chars3");
    cmd.string_arg("`");
    cmd.string_arg("$");
#if defined(_WIN32)
    REQUIRE(cmd.command_line() == "shell_escaped_chars3 \"`\" \"$\"");
#else
    REQUIRE(cmd.command_line() == "shell_escaped_chars3 \"\\`\" \"\\$\"");
#endif

#if 0
    // TODO: add checks for tab/newline/carriage-return chars in commands
    cmd.string_arg("\t");
    cmd.string_arg("\n");
    cmd.string_arg("\r");
#endif
}
