#include <vcpkg/base/files.h>

#include <vcpkg/commands.init-registry.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    constexpr CommandMetadata CommandInitRegistryMetadata{
        "x-init-registry",
        msgCmdInitRegistrySynopsis,
        {msgCmdInitRegistryExample1, "vcpkg x-init-registry ."},
        AutocompletePriority::Public,
        1,
        1,
        {},
        nullptr,
    };

    void command_init_registry_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        auto parsed_args = args.parse_arguments(CommandInitRegistryMetadata);

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
