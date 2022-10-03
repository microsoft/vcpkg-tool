#include <vcpkg/base/chrono.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.mac.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/uuid.h>
#include <vcpkg/base/view.h>

#include <vcpkg/commands.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>

#include <math.h>

#include <iterator>
#include <mutex>
#include <utility>

#if defined(_WIN32)
#pragma comment(lib, "version")
#pragma comment(lib, "winhttp")
#endif

namespace
{
    using namespace vcpkg;

    template<typename T, typename MetricEntry, size_t Size>
    constexpr StringLiteral get_metric_name(const T metric, const std::array<MetricEntry, Size>& entries) noexcept
    {
        auto metric_index = static_cast<size_t>(metric);
        if (metric_index < entries.size())
        {
            return entries[metric_index].name;
        }
        // abort() is used because Checks:: will call back into metrics machinery.
        abort();
    }

    static constexpr char METRICS_CONFIG_FILE_NAME[] = "config";

    void set_value_if_set(std::string& target, const Paragraph& p, const std::string& key)
    {
        auto position = p.find(key);
        if (position != p.end())
        {
            target = position->second.first;
        }
    }

    std::string get_os_version_string()
    {
#if defined(_WIN32)
        std::wstring path;
        path.resize(MAX_PATH);
        const auto n = GetSystemDirectoryW(&path[0], static_cast<UINT>(path.size()));
        path.resize(n);
        path += L"\\kernel32.dll";

        const auto versz = GetFileVersionInfoSizeW(path.c_str(), nullptr);
        if (versz == 0) return "";

        std::vector<char> verbuf;
        verbuf.resize(versz);

        if (!GetFileVersionInfoW(path.c_str(), 0, static_cast<DWORD>(verbuf.size()), &verbuf[0])) return "";

        void* rootblock;
        UINT rootblocksize;
        if (!VerQueryValueW(&verbuf[0], L"\\", &rootblock, &rootblocksize)) return "";

        auto rootblock_ffi = static_cast<VS_FIXEDFILEINFO*>(rootblock);

        return Strings::format("%d.%d.%d",
                               static_cast<int>(HIWORD(rootblock_ffi->dwProductVersionMS)),
                               static_cast<int>(LOWORD(rootblock_ffi->dwProductVersionMS)),
                               static_cast<int>(HIWORD(rootblock_ffi->dwProductVersionLS)));
#else
        return "unknown";
#endif
    }
}

namespace vcpkg
{
    const constexpr std::array<DefineMetricEntry, static_cast<size_t>(DefineMetric::COUNT)> all_define_metrics{{
        {DefineMetric::AssetSource, "asset-source"},
        {DefineMetric::BinaryCachingAws, "binarycaching_aws"},
        {DefineMetric::BinaryCachingAzBlob, "binarycaching_azblob"},
        {DefineMetric::BinaryCachingCos, "binarycaching_cos"},
        {DefineMetric::BinaryCachingDefault, "binarycaching_default"},
        {DefineMetric::BinaryCachingFiles, "binarycaching_files"},
        {DefineMetric::BinaryCachingGcs, "binarycaching_gcs"},
        {DefineMetric::BinaryCachingHttp, "binarycaching_http"},
        {DefineMetric::BinaryCachingNuget, "binarycaching_nuget"},
        {DefineMetric::BinaryCachingSource, "binarycaching-source"},
        {DefineMetric::ErrorVersioningDisabled, "error-versioning-disabled"},
        {DefineMetric::ErrorVersioningNoBaseline, "error-versioning-no-baseline"},
        {DefineMetric::GitHubRepository, "GITHUB_REPOSITORY"},
        {DefineMetric::ManifestBaseline, "manifest_baseline"},
        {DefineMetric::ManifestOverrides, "manifest_overrides"},
        {DefineMetric::ManifestVersionConstraint, "manifest_version_constraint"},
        {DefineMetric::RegistriesErrorCouldNotFindBaseline, "registries-error-could-not-find-baseline"},
        {DefineMetric::RegistriesErrorNoVersionsAtCommit, "registries-error-no-versions-at-commit"},
        {DefineMetric::VcpkgBinarySources, "VCPKG_BINARY_SOURCES"},
        {DefineMetric::VcpkgDefaultBinaryCache, "VCPKG_DEFAULT_BINARY_CACHE"},
        {DefineMetric::VcpkgNugetRepository, "VCPKG_NUGET_REPOSITORY"},
        {DefineMetric::VersioningErrorBaseline, "versioning-error-baseline"},
        {DefineMetric::VersioningErrorVersion, "versioning-error-version"},
        {DefineMetric::X_VcpkgRegistriesCache, "X_VCPKG_REGISTRIES_CACHE"},
        {DefineMetric::X_WriteNugetPackagesConfig, "x-write-nuget-packages-config"},
    }};

    const constexpr std::array<StringMetricEntry, static_cast<size_t>(StringMetric::COUNT)> all_string_metrics{{
        {StringMetric::BuildError, "build_error", "gsl:x64-windows"},
        {StringMetric::CommandArgs, "command_args", "0000000011111111aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffff"},
        {StringMetric::CommandContext, "command_context", "artifact"},
        {StringMetric::CommandName, "command_name", "z-preregister-telemetry"},
        {StringMetric::Error, "error", "build failed"},
        {StringMetric::InstallPlan_1,
         "installplan_1",
         "0000000011111111aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffff"},
        {StringMetric::ListFile, "listfile", "update to new format"},
        {StringMetric::RegistriesDefaultRegistryKind, "registries-default-registry-kind", "builtin-files"},
        {StringMetric::RegistriesKindsUsed, "registries-kinds-used", "git,filesystem"},
        {StringMetric::Title, "title", "title"},
        {StringMetric::UserMac, "user_mac", "0"},
        {StringMetric::VcpkgVersion, "vcpkg_version", "2999-12-31-unknownhash"},
        {StringMetric::Warning, "warning", "warning"},
    }};

    const constexpr std::array<BoolMetricEntry, static_cast<size_t>(BoolMetric::COUNT)> all_bool_metrics{{
        {BoolMetric::FeatureFlagBinaryCaching, "feature-flag-binarycaching"},
        {BoolMetric::FeatureFlagCompilerTracking, "feature-flag-compilertracking"},
        {BoolMetric::FeatureFlagManifests, "feature-flag-manifests"},
        {BoolMetric::FeatureFlagRegistries, "feature-flag-registries"},
        {BoolMetric::FeatureFlagVersions, "feature-flag-versions"},
        {BoolMetric::InstallManifestMode, "install_manifest_mode"},
        {BoolMetric::OptionOverlayPorts, "option_overlay_ports"},
    }};

    void MetricsSubmission::track_elapsed_us(double value)
    {
        if (!isfinite(value) || value <= 0.0)
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        elapsed_us = value;
    }

    void MetricsSubmission::track_buildtime(StringView name, double value)
    {
        const auto position = buildtimes.lower_bound(name);
        if (position == buildtimes.end() || name < position->first)
        {
            buildtimes.emplace_hint(position,
                                    std::piecewise_construct,
                                    std::forward_as_tuple(name.data(), name.size()),
                                    std::forward_as_tuple(value));
        }
        else
        {
            position->second = value;
        }
    }

    void MetricsSubmission::track_define(DefineMetric metric) { defines.insert(metric); }

    void MetricsSubmission::track_string(StringMetric metric, StringView value)
    {
        const auto position = strings.lower_bound(metric);
        if (position == strings.end() || metric < position->first)
        {
            strings.emplace_hint(position,
                                 std::piecewise_construct,
                                 std::forward_as_tuple(metric),
                                 std::forward_as_tuple(value.data(), value.size()));
        }
        else
        {
            position->second.assign(value.data(), value.size());
        }
    }

    void MetricsSubmission::track_bool(BoolMetric metric, bool value) { bools.insert_or_assign(metric, value); }

    void MetricsSubmission::merge(MetricsSubmission&& other)
    {
        if (other.elapsed_us != 0.0)
        {
            elapsed_us = other.elapsed_us;
        }

        buildtimes.merge(other.buildtimes);
        defines.merge(other.defines);
        strings.merge(other.strings);
        bools.merge(other.bools);
    }

    void MetricsCollector::track_elapsed_us(double value)
    {
        std::lock_guard<std::mutex> lock{mtx};
        submission.track_elapsed_us(value);
    }

    void MetricsCollector::track_buildtime(StringView name, double value)
    {
        std::lock_guard<std::mutex> lock{mtx};
        submission.track_buildtime(name, value);
    }

    void MetricsCollector::track_define(DefineMetric metric)
    {
        std::lock_guard<std::mutex> lock{mtx};
        submission.track_define(metric);
    }

    void MetricsCollector::track_string(StringMetric metric, StringView value)
    {
        std::lock_guard<std::mutex> lock{mtx};
        submission.track_string(metric, value);
    }

    void MetricsCollector::track_bool(BoolMetric metric, bool value)
    {
        std::lock_guard<std::mutex> lock{mtx};
        submission.track_bool(metric, value);
    }

    void MetricsCollector::track_submission(MetricsSubmission&& submission_)
    {
        std::lock_guard<std::mutex> lock{mtx};
        submission.merge(std::move(submission_));
    }

    MetricsSubmission MetricsCollector::get_submission() const
    {
        Optional<MetricsSubmission> result;
        {
            std::lock_guard<std::mutex> lock{mtx};
            result.emplace(submission);
        } // unlock

        return std::move(result).value_or_exit(VCPKG_LINE_INFO);
    }

    MetricsCollector& get_global_metrics_collector() noexcept
    {
        static MetricsCollector g_metrics_collector;
        return g_metrics_collector;
    }

    void MetricsUserConfig::to_string(std::string& target) const
    {
        fmt::format_to(std::back_inserter(target),
                       "User-Id: {}\n"
                       "User-Since: {}\n"
                       "Mac-Hash: {}\n"
                       "Survey-Completed: {}\n",
                       user_id,
                       user_time,
                       user_mac,
                       last_completed_survey);
    }

    std::string MetricsUserConfig::to_string() const
    {
        std::string ret;
        to_string(ret);
        return ret;
    }

    void MetricsUserConfig::try_write(Filesystem& fs) const
    {
        const auto& maybe_user_dir = get_user_configuration_home();
        if (auto p_user_dir = maybe_user_dir.get())
        {
            fs.create_directories(*p_user_dir, IgnoreErrors{});
            fs.write_contents(*p_user_dir / METRICS_CONFIG_FILE_NAME, to_string(), IgnoreErrors{});
        }
    }

    bool MetricsUserConfig::fill_in_system_values()
    {
        bool result = false;
        // config file not found, could not be read, or invalid
        if (user_id.empty() || user_time.empty())
        {
            user_id = generate_random_UUID();
            user_time = CTime::now_string();
            result = true;
        }

        if (user_mac.empty() || user_mac == "{}")
        {
            user_mac = get_user_mac_hash();
            result = true;
        }

        return result;
    }

    MetricsUserConfig try_parse_metrics_user(StringView content)
    {
        MetricsUserConfig ret;
        auto maybe_paragraph = Paragraphs::parse_single_merged_paragraph(content, "userconfig");
        if (const auto p = maybe_paragraph.get())
        {
            const auto& paragraph = *p;
            set_value_if_set(ret.user_id, paragraph, "User-Id");
            set_value_if_set(ret.user_time, paragraph, "User-Since");
            set_value_if_set(ret.user_mac, paragraph, "Mac-Hash");
            set_value_if_set(ret.last_completed_survey, paragraph, "Survey-Completed");
        }

        return ret;
    }

    MetricsUserConfig try_read_metrics_user(const Filesystem& fs)
    {
        const auto& maybe_user_dir = get_user_configuration_home();
        if (auto p_user_dir = maybe_user_dir.get())
        {
            std::error_code ec;
            const auto content = fs.read_contents(*p_user_dir / METRICS_CONFIG_FILE_NAME, ec);
            if (!ec)
            {
                return try_parse_metrics_user(content);
            }
        }

        return MetricsUserConfig{};
    }

    MetricsSessionData MetricsSessionData::from_system()
    {
        MetricsSessionData result;
        result.submission_time = CTime::now_string();
        StringLiteral os_name =
#if defined(_WIN32)
            "Windows";
#elif defined(__APPLE__)
            "OSX";
#elif defined(__linux__)
            "Linux";
#elif defined(__FreeBSD__)
            "FreeBSD";
#elif defined(__unix__)
            "Unix";
#else
            "Other";
#endif

        result.os_version.assign(os_name.data(), os_name.size());
        result.os_version.push_back('-');
        result.os_version.append(get_os_version_string());

        result.session_id = generate_random_UUID();
        return result;
    }

    std::string format_metrics_payload(const MetricsUserConfig& user,
                                       const MetricsSessionData& session,
                                       const MetricsSubmission& submission)
    {
        Json::Array arr = Json::Array();
        Json::Object& obj = arr.push_back(Json::Object());

        obj.insert("ver", Json::Value::integer(1));
        obj.insert("name", Json::Value::string("Microsoft.ApplicationInsights.Event"));
        obj.insert("time", Json::Value::string(session.submission_time));
        obj.insert("sampleRate", Json::Value::number(100.0));
        obj.insert("seq", Json::Value::string("0:0"));
        obj.insert("iKey", Json::Value::string("b4e88960-4393-4dd9-ab8e-97e8fe6d7603"));
        obj.insert("flags", Json::Value::integer(0));

        Json::Object& tags = obj.insert("tags", Json::Object());

        tags.insert("ai.device.os", Json::Value::string("Other"));

        tags.insert("ai.device.osVersion", Json::Value::string(session.os_version));
        tags.insert("ai.session.id", Json::Value::string(session.session_id));
        tags.insert("ai.user.id", Json::Value::string(user.user_id));
        tags.insert("ai.user.accountAcquisitionDate", Json::Value::string(user.user_time));

        Json::Object& data = obj.insert("data", Json::Object());

        data.insert("baseType", Json::Value::string("EventData"));
        Json::Object& base_data = data.insert("baseData", Json::Object());

        base_data.insert("ver", Json::Value::integer(2));
        base_data.insert("name", Json::Value::string("commandline_test7"));
        Json::Object& properties = base_data.insert("properties", Json::Object());
        for (auto&& define_property : submission.defines)
        {
            properties.insert_or_replace(get_metric_name(define_property, all_define_metrics),
                                         Json::Value::string("defined"));
        }

        properties.insert_or_replace(get_metric_name(StringMetric::UserMac, all_string_metrics),
                                     Json::Value::string(user.user_mac));
        for (auto&& string_property : submission.strings)
        {
            properties.insert_or_replace(get_metric_name(string_property.first, all_string_metrics),
                                         Json::Value::string(string_property.second));
        }

        for (auto&& bool_property : submission.bools)
        {
            properties.insert_or_replace(get_metric_name(bool_property.first, all_bool_metrics),
                                         Json::Value::boolean(bool_property.second));
        }

        if (!submission.buildtimes.empty())
        {
            Json::Array buildtime_names;
            Json::Array buildtime_times;
            for (auto&& buildtime : submission.buildtimes)
            {
                buildtime_names.push_back(Json::Value::string(buildtime.first));
                buildtime_times.push_back(Json::Value::number(buildtime.second));
            }

            properties.insert("buildnames_1", buildtime_names);
            properties.insert("buildtimes", buildtime_times);
        }

        Json::Object& measurements = base_data.insert("measurements", Json::Object());
        if (submission.elapsed_us != 0.0)
        {
            measurements.insert_or_replace("elapsed_us", Json::Value::number(submission.elapsed_us));
        }

        return Json::stringify(arr);
    }

    std::atomic<bool> g_should_send_metrics =
#if defined(NDEBUG)
        true
#else
        false
#endif
        ;
    std::atomic<bool> g_should_print_metrics = false;
    std::atomic<bool> g_metrics_enabled = false;

#if defined(_WIN32)
    void winhttp_upload_metrics(StringView payload)
    {
        HINTERNET connect = nullptr, request = nullptr;
        BOOL results = FALSE;

        const HINTERNET session = WinHttpOpen(
            L"vcpkg/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

        unsigned long secure_protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        if (session && WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &secure_protocols, sizeof(DWORD)))
        {
            connect = WinHttpConnect(session, L"dc.services.visualstudio.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        }

        if (connect)
        {
            request = WinHttpOpenRequest(connect,
                                         L"POST",
                                         L"/v2/track",
                                         nullptr,
                                         WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE);
        }

        if (request)
        {
            auto mutable_payload = payload.to_string();
            if (MAXDWORD <= mutable_payload.size()) abort();
            std::wstring hdrs = L"Content-Type: application/json\r\n";
            results = WinHttpSendRequest(request,
                                         hdrs.c_str(),
                                         static_cast<DWORD>(hdrs.size()),
                                         static_cast<void*>(mutable_payload.data()),
                                         static_cast<DWORD>(mutable_payload.size()),
                                         static_cast<DWORD>(mutable_payload.size()),
                                         0);
        }

        if (results)
        {
            results = WinHttpReceiveResponse(request, nullptr);
        }

        DWORD http_code = 0, junk = sizeof(DWORD);

        if (results)
        {
            results = WinHttpQueryHeaders(request,
                                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                          nullptr,
                                          &http_code,
                                          &junk,
                                          WINHTTP_NO_HEADER_INDEX);
        }

        std::vector<char> response_buffer;
        if (results)
        {
            DWORD available_data = 0, read_data = 0, total_data = 0;
            while ((results = WinHttpQueryDataAvailable(request, &available_data)) == TRUE && available_data > 0)
            {
                response_buffer.resize(response_buffer.size() + available_data);

                results = WinHttpReadData(request, &response_buffer.data()[total_data], available_data, &read_data);

                if (!results)
                {
                    break;
                }

                total_data += read_data;

                response_buffer.resize(total_data);
            }
        }

        if (!results)
        {
#ifndef NDEBUG
            __debugbreak();
            auto err = GetLastError();
            fprintf(stderr, "[DEBUG] failed to connect to server: %08lu\n", err);
#endif // NDEBUG
        }

        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
    }
#endif // ^^^ _WIN32

    void flush_global_metrics(Filesystem& fs)
    {
        if (!g_metrics_enabled.load())
        {
            return;
        }

        auto user = try_read_metrics_user(fs);
        if (user.fill_in_system_values())
        {
            user.try_write(fs);
        }

        auto session = MetricsSessionData::from_system();

        auto submission = get_global_metrics_collector().get_submission();
        const std::string payload = format_metrics_payload(user, session, submission);
        if (g_should_print_metrics.load())
        {
            fprintf(stderr, "%s\n", payload.c_str());
        }

        if (!g_should_send_metrics)
        {
            return;
        }

#if defined(_WIN32)
        wchar_t temp_folder[MAX_PATH];
        GetTempPathW(MAX_PATH, temp_folder);

        const Path temp_folder_path = Path(Strings::to_utf8(temp_folder)) / "vcpkg";
        const Path temp_folder_path_exe = temp_folder_path / "vcpkg-" VCPKG_BASE_VERSION_AS_STRING ".exe";
#endif

        std::error_code ec;
#if defined(_WIN32)
        fs.create_directories(temp_folder_path, ec);
        if (ec) return;
        fs.copy_file(get_exe_path_of_current_process(), temp_folder_path_exe, CopyOptions::skip_existing, ec);
        if (ec) return;
#else
        if (!fs.exists("/tmp", IgnoreErrors{})) return;
        const Path temp_folder_path = "/tmp/vcpkg";
        fs.create_directory(temp_folder_path, IgnoreErrors{});
#endif
        const Path vcpkg_metrics_txt_path = temp_folder_path / ("vcpkg" + generate_random_UUID() + ".txt");
        fs.write_contents(vcpkg_metrics_txt_path, payload, ec);
        if (ec) return;

#if defined(_WIN32)
        Command builder;
        builder.string_arg(temp_folder_path_exe);
        builder.string_arg("x-upload-metrics");
        builder.string_arg(vcpkg_metrics_txt_path);
        cmd_execute_background(builder);
#else
        // TODO: convert to cmd_execute_background or something.
        auto curl = Command("curl")
                        .string_arg("https://dc.services.visualstudio.com/v2/track")
                        .string_arg("--max-time")
                        .string_arg("3")
                        .string_arg("-H")
                        .string_arg("Content-Type: application/json")
                        .string_arg("-X")
                        .string_arg("POST")
                        .string_arg("--tlsv1.2")
                        .string_arg("--data")
                        .string_arg(Strings::concat("@", vcpkg_metrics_txt_path))
                        .raw_arg(">/dev/null")
                        .raw_arg("2>&1");
        auto remove = Command("rm").string_arg(vcpkg_metrics_txt_path);
        Command cmd_line;
        cmd_line.raw_arg("(").raw_arg(curl.command_line()).raw_arg(";").raw_arg(remove.command_line()).raw_arg(") &");
        cmd_execute_clean(cmd_line);
#endif
    }
}
