#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/setup-messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/uuid.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/metrics.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    void track_telemetry(const Filesystem& fs, const Path& telemetry_file_path)
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

        if (auto acquired_artifacts = pparsed->get(JsonIdAcquiredArtifacts))
        {
            if (auto maybe_acquired_string = acquired_artifacts->maybe_string())
            {
                get_global_metrics_collector().track_string(StringMetric::AcquiredArtifacts, *maybe_acquired_string);
            }
            else
            {
                Debug::println("Acquired artifacts was not a string.");
            }
        }
        else
        {
            Debug::println("No artifacts acquired.");
        }

        if (auto activated_artifacts = pparsed->get(JsonIdActivatedArtifacts))
        {
            if (auto maybe_activated_string = activated_artifacts->maybe_string())
            {
                get_global_metrics_collector().track_string(StringMetric::ActivatedArtifacts, *maybe_activated_string);
            }
            else
            {
                Debug::println("Activated artifacts was not a string.");
            }
        }
        else
        {
            Debug::println("No artifacts activated.");
        }
    }

    constexpr const StringLiteral* ArtifactOperatingSystemsSwitchNamesStorage[] = {
        &SwitchWindows, &SwitchOsx, &SwitchLinux, &SwitchFreeBsd};
    constexpr const StringLiteral* ArtifactHostPlatformSwitchNamesStorage[] = {
        &SwitchX86, &SwitchX64, &SwitchArm, &SwitchArm64};
    constexpr const StringLiteral* ArtifactTargetPlatformSwitchNamesStorage[] = {
        &SwitchTargetX86, &SwitchTargetX64, &SwitchTargetArm, &SwitchTargetArm64};

    bool more_than_one_mapped(View<const StringLiteral*> candidates,
                              const std::set<StringLiteral, std::less<>>& switches)
    {
        bool seen = false;
        for (auto&& candidate : candidates)
        {
            if (Util::Sets::contains(switches, *candidate))
            {
                if (seen)
                {
                    return true;
                }

                seen = true;
            }
        }

        return false;
    }
} // unnamed namespace

namespace vcpkg
{
    Optional<Path> provision_node_component(DiagnosticContext& context,
                                            Path script_path, // intentionally declared exe_path in the header
                                            const AssetCachingSettings& asset_cache_settings,
                                            const Filesystem& fs,
                                            const Path& download_root,
                                            StringLiteral script_name,
                                            const Optional<std::string>& script_sha512)
    {
        // The .mjs may exist if this is the one-liner, the Visual Studio distribution, or local development
        script_path.replace_filename(fmt::format("{}.mjs", script_name));
        script_path.make_preferred();
        if (fs.exists(script_path, VCPKG_LINE_INFO))
        {
            return script_path;
        }

        const char* url_prefix;
        std::string filename = script_name.to_string();
        filename.push_back('-');
        if (auto sha = script_sha512.get())
        {
            // this is an official release
            url_prefix = "https://github.com/microsoft/vcpkg-tool/releases/download/" VCPKG_BASE_VERSION_AS_STRING;
            filename.append(sha->data(), sha->size());
        }
        else
        {
            // not an official release, always use latest
            url_prefix = "https://github.com/microsoft/vcpkg-tool/releases/latest/download";
            fmt::format_to(std::back_inserter(filename), "{}", vcpkg::get_process_id());
        }

        filename.append(".mjs");
        Path download_path = download_root / filename;
        if (auto sha = script_sha512.get())
        {
            if (fs.exists(download_path, VCPKG_LINE_INFO))
            {
                auto maybe_actual_hash =
                    Hash::get_file_hash_required(context, fs, download_path, Hash::Algorithm::Sha512);
                if (auto actual_hash = maybe_actual_hash.get())
                {
                    if (*actual_hash == *sha)
                    {
                        return download_path;
                    }
                }
            }
        }

        fs.remove(download_path, VCPKG_LINE_INFO);
        std::string url = fmt::format("{}/{}.mjs", url_prefix, script_name);
        if (download_file_asset_cached(
                context, null_sink, asset_cache_settings, fs, url, {}, download_path, filename, script_sha512))
        {
            return download_path;
        }

        fs.remove(download_path, VCPKG_LINE_INFO);
        return nullopt;
    }

    int run_configure_environment_command(const VcpkgPaths& paths, View<std::string> args)
    {
        msg::println_warning(msgVcpkgCeIsExperimental);
        auto& fs = paths.get_filesystem();

        auto exe_path = get_exe_path_of_current_process();
        Optional<std::string> script_sha512;
#if defined(VCPKG_ARTIFACTS_SHA)
        script_sha512.emplace(MACRO_TO_STRING(VCPKG_ARTIFACTS_SHA));
#endif

        auto maybe_vcpkg_artifacts_path = provision_node_component(console_diagnostic_context,
                                                                   exe_path,
                                                                   paths.get_asset_cache_settings(),
                                                                   fs,
                                                                   paths.downloads,
                                                                   "vcpkg-artifacts",
                                                                   script_sha512);
        auto vcpkg_artifacts_path = maybe_vcpkg_artifacts_path.get();
        if (!vcpkg_artifacts_path)
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgArtifactsBootstrapFailed);
        }

        auto temp_directory = fs.create_or_get_temp_directory(VCPKG_LINE_INFO);

        auto cmd = Command{paths.get_tool_exe(Tools::NODE, out_sink)};
        cmd.string_arg(*vcpkg_artifacts_path);
        cmd.forwarded_args(args);
        if (Debug::g_debugging)
        {
            cmd.string_arg("--debug");
        }

        Optional<Path> maybe_telemetry_file_path;
        if (g_metrics_enabled.load())
        {
            auto& p = maybe_telemetry_file_path.emplace(temp_directory /
                                                        (generate_random_UUID() + "_artifacts_telemetry.txt"));
            cmd.string_arg("--z-telemetry-file").string_arg(p);
        }

        cmd.string_arg("--vcpkg-root").string_arg(paths.root);
        cmd.string_arg("--z-vcpkg-command").string_arg(exe_path);

        cmd.string_arg("--z-vcpkg-artifacts-root").string_arg(paths.artifacts());
        cmd.string_arg("--z-vcpkg-downloads").string_arg(paths.downloads);
        cmd.string_arg("--z-vcpkg-registries-cache").string_arg(paths.registries_cache());
        cmd.string_arg("--z-next-previous-environment")
            .string_arg(temp_directory / (generate_random_UUID() + "_previous_environment.txt"));
        cmd.string_arg("--z-global-config").string_arg(paths.global_config());

        auto maybe_file = msg::get_loaded_file();
        if (!maybe_file.empty())
        {
            auto temp_file = temp_directory / "messages.json";
            fs.write_contents(temp_file, maybe_file, VCPKG_LINE_INFO);
            cmd.string_arg("--language").string_arg(temp_file);
        }

        ProcessLaunchSettings settings;
        settings.working_directory = paths.original_cwd;
        const auto node_result = cmd_execute(cmd, settings).value_or_exit(VCPKG_LINE_INFO);
        if (auto telemetry_file_path = maybe_telemetry_file_path.get())
        {
            track_telemetry(fs, *telemetry_file_path);
        }

        if constexpr (std::is_signed_v<decltype(node_result)>)
        {
            // workaround some systems which only keep the lower 7 bits
            if (node_result < 0 || node_result > 127)
            {
                return 1;
            }

            return node_result;
        }
        else
        {
            return static_cast<int>(node_result);
        }
    }

    void forward_common_artifacts_arguments(std::vector<std::string>& appended_to, const ParsedArguments& parsed)
    {
        auto&& switches = parsed.switches;
        for (auto&& parsed_switch : switches)
        {
            appended_to.push_back(fmt::format("--{}", parsed_switch));
        }

        if (more_than_one_mapped(ArtifactOperatingSystemsSwitchNamesStorage, parsed.switches))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgArtifactsSwitchOnlyOneOperatingSystem);
        }

        if (more_than_one_mapped(ArtifactHostPlatformSwitchNamesStorage, parsed.switches))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgArtifactsSwitchOnlyOneHostPlatform);
        }

        if (more_than_one_mapped(ArtifactTargetPlatformSwitchNamesStorage, parsed.switches))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgArtifactsSwitchOnlyOneTargetPlatform);
        }

        for (auto&& parsed_option : parsed.settings)
        {
            appended_to.push_back(fmt::format("--{}", parsed_option.first));
            appended_to.push_back(parsed_option.second);
        }
    }
} // namespace vcpkg
