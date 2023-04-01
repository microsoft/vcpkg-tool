#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/setup-messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/uuid.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/metrics.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;
    void track_telemetry(Filesystem& fs, const Path& telemetry_file_path)
    {
        std::error_code ec;
        auto telemetry_file = fs.read_contents(telemetry_file_path, ec);
        if (ec)
        {
            Debug::println("Telemetry file couldn't be read: " + ec.message());
            return;
        }

        auto maybe_parsed = Json::parse_object(telemetry_file, telemetry_file_path);
        auto pparsed = maybe_parsed.get();

        if (!pparsed)
        {
            Debug::println("Telemetry file couldn't be parsed: " + maybe_parsed.error().data());
            return;
        }

        auto acquired_artifacts = pparsed->get("acquired_artifacts");
        if (acquired_artifacts)
        {
            if (acquired_artifacts->is_string())
            {
                get_global_metrics_collector().track_string(StringMetric::AcquiredArtifacts,
                                                            acquired_artifacts->string(VCPKG_LINE_INFO));
            }
            Debug::println("Acquired artifacts was not a string.");
        }
        else
        {
            Debug::println("No artifacts acquired.");
        }

        auto activated_artifacts = pparsed->get("activated_artifacts");
        if (activated_artifacts)
        {
            if (activated_artifacts->is_string())
            {
                get_global_metrics_collector().track_string(StringMetric::ActivatedArtifacts,
                                                            activated_artifacts->string(VCPKG_LINE_INFO));
            }
            Debug::println("Activated artifacts was not a string.");
        }
        else
        {
            Debug::println("No artifacts activated.");
        }
    }
}

namespace vcpkg
{
    int run_configure_environment_command(const VcpkgPaths& paths, View<std::string> args)
    {
        msg::println_warning(msgVcpkgCeIsExperimental);
        auto& fs = paths.get_filesystem();

        // if artifacts is deployed in development, with Visual Studio, or with the One Liner, it will be deployed here
        Path vcpkg_artifacts_path = get_exe_path_of_current_process();
        vcpkg_artifacts_path.replace_filename("vcpkg-artifacts/main.js");
        vcpkg_artifacts_path.make_preferred();
        if (!fs.exists(vcpkg_artifacts_path, VCPKG_LINE_INFO))
        {
            // otherwise, if this is an official build we can try to extract a copy of vcpkg-artifacts out of the
            // matching standalone bundle
            // FIXME
            // otherwise, fail
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgArtifactsNotInstalled);
        }

        auto temp_directory = fs.create_or_get_temp_directory(VCPKG_LINE_INFO);

        Command cmd_run(paths.get_tool_exe(Tools::NODE, stdout_sink));
        cmd_run.string_arg(vcpkg_artifacts_path);
        cmd_run.forwarded_args(args);
        if (Debug::g_debugging)
        {
            cmd_run.string_arg("--debug");
        }

        Optional<Path> maybe_telemetry_file_path;
        if (g_metrics_enabled.load())
        {
            auto& p = maybe_telemetry_file_path.emplace(temp_directory /
                                                        (generate_random_UUID() + "_artifacts_telemetry.txt"));
            cmd_run.string_arg("--z-telemetry-file").string_arg(p);
        }

        cmd_run.string_arg("--vcpkg-root").string_arg(paths.root);
        cmd_run.string_arg("--z-vcpkg-command").string_arg(get_exe_path_of_current_process());

        cmd_run.string_arg("--z-vcpkg-artifacts-root").string_arg(paths.artifacts());
        cmd_run.string_arg("--z-vcpkg-downloads").string_arg(paths.downloads);
        cmd_run.string_arg("--z-vcpkg-registries-cache").string_arg(paths.registries_cache());
        cmd_run.string_arg("--z-next-previous-environment")
            .string_arg(temp_directory / (generate_random_UUID() + "_previous_environment.txt"));
        cmd_run.string_arg("--z-global-config").string_arg(paths.global_config());

        auto maybe_file = msg::get_loaded_file();
        if (!maybe_file.empty())
        {
            auto temp_file = temp_directory / "messages.json";
            fs.write_contents(temp_file, maybe_file, VCPKG_LINE_INFO);
            cmd_run.string_arg("--language").string_arg(temp_file);
        }

        auto result = cmd_execute(cmd_run, WorkingDirectory{paths.original_cwd}).value_or_exit(VCPKG_LINE_INFO);
        if (auto telemetry_file_path = maybe_telemetry_file_path.get())
        {
            track_telemetry(fs, *telemetry_file_path);
        }

        return result;
    }

    int run_configure_environment_command(const VcpkgPaths& paths, StringView arg0, View<std::string> args)
    {
        std::vector<std::string> all_args;
        all_args.reserve(args.size() + 1);
        all_args.emplace_back(arg0.data(), arg0.size());
        all_args.insert(all_args.end(), args.begin(), args.end());
        return run_configure_environment_command(paths, all_args);
    }
}
