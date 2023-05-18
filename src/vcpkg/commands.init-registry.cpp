#include <vcpkg/base/files.h>

#include <vcpkg/commands.init-registry.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::InitRegistry
{
    static const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string(R"(x-init-registry .)"); },
        1,
        1,
        {{}, {}, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs)
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

        const Path string_argument = parsed_args.command_arguments.front();
        const auto path = fs.current_path(VCPKG_LINE_INFO) / string_argument;
        if (!fs.exists(path / ".git", IgnoreErrors{}))
        {
            msg::println_error(msgInitRegistryFailedNoRepo, msg::path = path, msg::command_line = string_argument);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        const auto ports = path / "ports";
        const auto baseline = path / "versions/baseline.json";
        if (!fs.exists(ports, IgnoreErrors{}))
        {
            fs.create_directories(ports, VCPKG_LINE_INFO);
        }
        if (!fs.exists(baseline, IgnoreErrors{}))
        {
            const auto content = R"({
  "default": {}
})";
            fs.write_contents_and_dirs(baseline, content, VCPKG_LINE_INFO);
        }
        msg::println(msgRegistryCreated, msg::path = path);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
