#include <vcpkg/base/chrono.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.mac.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/uuid.h>
#include <vcpkg/base/view.h>

#include <vcpkg/commands.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/metrics.h>
#include <vcpkg/userconfig.h>

#if defined(_WIN32)
#pragma comment(lib, "version")
#pragma comment(lib, "winhttp")
#endif

namespace
{
    using namespace vcpkg;

    template<typename T>
    StringLiteral get_metric_name(const T metric, View<MetricEntry<T>> entries)
    {
        for (auto&& entry : entries)
        {
            if (entry.metric == metric)
            {
                return entry.name;
            }
        }

        // All metric enums should have corresponding names
        Debug::println("Error: Metric is missing a name");
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
}

namespace vcpkg
{
    LockGuarded<Metrics> g_metrics;

    constexpr View<MetricEntry<Metrics::DefineMetric>> Metrics::get_define_metrics()
    {
        constexpr std::array<MetricEntry<Metrics::DefineMetric>, static_cast<size_t>(Metrics::DefineMetric_COUNT)>
            ENTRIES{
                MetricEntry<Metrics::DefineMetric>{Metrics::AssetSource, "asset-source"},
                MetricEntry<Metrics::DefineMetric>{Metrics::BinaryCachingAws, "binarycaching_aws"},
                MetricEntry<Metrics::DefineMetric>{Metrics::BinaryCachingAzBlob, "binarycaching_azblob"},
                MetricEntry<Metrics::DefineMetric>{Metrics::BinaryCachingCos, "binarycaching_cos"},
                MetricEntry<Metrics::DefineMetric>{Metrics::BinaryCachingDefault, "binarycaching_default"},
                MetricEntry<Metrics::DefineMetric>{Metrics::BinaryCachingFiles, "binarycaching_files"},
                MetricEntry<Metrics::DefineMetric>{Metrics::BinaryCachingGcs, "binarycaching_gcs"},
                MetricEntry<Metrics::DefineMetric>{Metrics::BinaryCachingHttp, "binarycaching_http"},
                MetricEntry<Metrics::DefineMetric>{Metrics::BinaryCachingNuget, "binarycaching_nuget"},
                MetricEntry<Metrics::DefineMetric>{Metrics::BinaryCachingSource, "binarycaching-source"},
                MetricEntry<Metrics::DefineMetric>{Metrics::ErrorVersioningDisabled, "error-versioning-disabled"},
                MetricEntry<Metrics::DefineMetric>{Metrics::ErrorVersioningNoBaseline, "error-versioning-no-baseline"},
                MetricEntry<Metrics::DefineMetric>{Metrics::GitHubRepository, "GITHUB_REPOSITORY"},
                MetricEntry<Metrics::DefineMetric>{Metrics::ManifestBaseline, "manifest_baseline"},
                MetricEntry<Metrics::DefineMetric>{Metrics::ManifestOverrides, "manifest_overrides"},
                MetricEntry<Metrics::DefineMetric>{Metrics::ManifestVersionConstraint, "manifest_version_constraint"},
                MetricEntry<Metrics::DefineMetric>{Metrics::RegistriesErrorCouldNotFindBaseline,
                                                   "registries-error-could-not-find-baseline"},
                MetricEntry<Metrics::DefineMetric>{Metrics::RegistriesErrorNoVersionsAtCommit,
                                                   "registries-error-no-versions-at-commit"},
                MetricEntry<Metrics::DefineMetric>{Metrics::VcpkgBinarySources, "VCPKG_BINARY_SOURCES"},
                MetricEntry<Metrics::DefineMetric>{Metrics::VcpkgDefaultBinaryCache, "VCPKG_DEFAULT_BINARY_CACHE"},
                MetricEntry<Metrics::DefineMetric>{Metrics::VcpkgNugetRepository, "VCPKG_NUGET_REPOSITORY"},
                MetricEntry<Metrics::DefineMetric>{Metrics::VersioningErrorBaseline, "versioning-error-baseline"},
                MetricEntry<Metrics::DefineMetric>{Metrics::VersioningErrorVersion, "versioning-error-version"},
                MetricEntry<Metrics::DefineMetric>{Metrics::X_VcpkgRegistriesCache, "X_VCPKG_REGISTRIES_CACHE"},
                MetricEntry<Metrics::DefineMetric>{Metrics::X_WriteNugetPackagesConfig,
                                                   "x-write-nuget-packages-config"},
            };
        return {ENTRIES.data(), ENTRIES.size()};
    }

    constexpr View<MetricEntry<Metrics::StringMetric>> Metrics::get_string_metrics()
    {
        constexpr std::array<MetricEntry<Metrics::StringMetric>, static_cast<size_t>(Metrics::StringMetric_COUNT)>
            ENTRIES{
                MetricEntry<Metrics::StringMetric>{Metrics::BuildError, "build_error"},
                MetricEntry<Metrics::StringMetric>{Metrics::CommandArgs, "command_args"},
                MetricEntry<Metrics::StringMetric>{Metrics::CommandContext, "command_context"},
                MetricEntry<Metrics::StringMetric>{Metrics::CommandName, "command_name"},
                MetricEntry<Metrics::StringMetric>{Metrics::Error, "error"},
                MetricEntry<Metrics::StringMetric>{Metrics::InstallPlan_1, "installplan_1"},
                MetricEntry<Metrics::StringMetric>{Metrics::ListFile, "listfile"},
                MetricEntry<Metrics::StringMetric>{Metrics::RegistriesDefaultRegistryKind,
                                                   "registries-default-registry-kind"},
                MetricEntry<Metrics::StringMetric>{Metrics::RegistriesKindsUsed, "registries-kinds-used"},
                MetricEntry<Metrics::StringMetric>{Metrics::Title, "title"},
                MetricEntry<Metrics::StringMetric>{Metrics::UserMac, "user_mac"},
                MetricEntry<Metrics::StringMetric>{Metrics::VcpkgVersion, "vcpkg_version"},
                MetricEntry<Metrics::StringMetric>{Metrics::Warning, "warning"},
            };
        return {ENTRIES.data(), ENTRIES.size()};
    }

    constexpr View<MetricEntry<Metrics::BoolMetric>> Metrics::get_bool_metrics()
    {
        constexpr std::array<MetricEntry<Metrics::BoolMetric>, static_cast<size_t>(Metrics::BoolMetric_COUNT)> ENTRIES{
            MetricEntry<Metrics::BoolMetric>{Metrics::InstallManifestMode, "install_manifest_mode"},
            MetricEntry<Metrics::BoolMetric>{Metrics::OptionOverlayPorts, "option_overlay_ports"},
        };
        return {ENTRIES.data(), ENTRIES.size()};
    }

    static std::string get_current_date_time_string()
    {
        auto maybe_time = CTime::get_current_date_time();
        if (auto ptime = maybe_time.get())
        {
            return ptime->to_string();
        }

        return "";
    }

    static const std::string& get_session_id()
    {
        static const std::string ID = generate_random_UUID();
        return ID;
    }

    static std::string get_os_version_string()
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

    struct MetricMessage
    {
        std::string user_id;
        std::string user_timestamp;

        Json::Object properties;
        Json::Object measurements;

        Json::Array buildtime_names;
        Json::Array buildtime_times;

        void track_property(StringView name, const std::string& value)
        {
            properties.insert_or_replace(name, Json::Value::string(value));
        }

        void track_property(StringView name, bool value)
        {
            properties.insert_or_replace(name, Json::Value::boolean(value));
        }

        void track_metric(StringView name, double value)
        {
            measurements.insert_or_replace(name, Json::Value::number(value));
        }

        void track_buildtime(StringView name, double value)
        {
            buildtime_names.push_back(Json::Value::string(name));
            buildtime_times.push_back(Json::Value::number(value));
        }
        void track_feature(StringView name, bool value)
        {
            properties.insert(Strings::concat("feature-flag-", name), Json::Value::boolean(value));
        }

        std::string format_event_data_template() const
        {
            auto props_plus_buildtimes = properties;
            if (buildtime_names.size() > 0)
            {
                props_plus_buildtimes.insert("buildnames_1", buildtime_names);
                props_plus_buildtimes.insert("buildtimes", buildtime_times);
            }

            Json::Array arr = Json::Array();
            Json::Object& obj = arr.push_back(Json::Object());

            obj.insert("ver", Json::Value::integer(1));
            obj.insert("name", Json::Value::string("Microsoft.ApplicationInsights.Event"));
            obj.insert("time", Json::Value::string(get_current_date_time_string()));
            obj.insert("sampleRate", Json::Value::number(100.0));
            obj.insert("seq", Json::Value::string("0:0"));
            obj.insert("iKey", Json::Value::string("b4e88960-4393-4dd9-ab8e-97e8fe6d7603"));
            obj.insert("flags", Json::Value::integer(0));

            {
                Json::Object& tags = obj.insert("tags", Json::Object());

                tags.insert("ai.device.os", Json::Value::string("Other"));

                const char* os_name =
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

                tags.insert("ai.device.osVersion",
                            Json::Value::string(Strings::format("%s-%s", os_name, get_os_version_string())));
                tags.insert("ai.session.id", Json::Value::string(get_session_id()));
                tags.insert("ai.user.id", Json::Value::string(user_id));
                tags.insert("ai.user.accountAcquisitionDate", Json::Value::string(user_timestamp));
            }

            {
                Json::Object& data = obj.insert("data", Json::Object());

                data.insert("baseType", Json::Value::string("EventData"));
                Json::Object& base_data = data.insert("baseData", Json::Object());

                base_data.insert("ver", Json::Value::integer(2));
                base_data.insert("name", Json::Value::string("commandline_test7"));
                base_data.insert("properties", std::move(props_plus_buildtimes));
                base_data.insert("measurements", measurements);
            }

            return Json::stringify(arr);
        }
    };

    static MetricMessage g_metricmessage;
    static bool g_should_send_metrics =
#if defined(NDEBUG)
        true
#else
        false
#endif
        ;
    static bool g_should_print_metrics = false;
    static std::atomic<bool> g_metrics_disabled = true;

    void Metrics::set_send_metrics(bool should_send_metrics) { g_should_send_metrics = should_send_metrics; }

    void Metrics::set_print_metrics(bool should_print_metrics) { g_should_print_metrics = should_print_metrics; }

    static bool g_initializing_metrics = false;

    void Metrics::enable()
    {
        {
            LockGuardPtr<Metrics> metrics(g_metrics);
            if (g_initializing_metrics) return;
            g_initializing_metrics = true;
        }

        // Execute this body exactly once
        auto& fs = get_real_filesystem();
        auto config = UserConfig::try_read_data(fs);

        bool write_config = false;

        // config file not found, could not be read, or invalid
        if (config.user_id.empty() || config.user_time.empty())
        {
            config.user_id = generate_random_UUID();
            config.user_time = get_current_date_time_string();
            write_config = true;
        }

        // For a while we had a bug where we always set "{}" without attempting to get a MAC address.
        // We will attempt to get a MAC address and store a "0" if we fail.
        if (config.user_mac.empty() || config.user_mac == "{}")
        {
            config.user_mac = get_user_mac_hash();
            write_config = true;
        }

        if (write_config)
        {
            config.try_write_data(fs);
        }

        {
            LockGuardPtr<Metrics> metrics(g_metrics);
            g_metricmessage.user_id = config.user_id;
            g_metricmessage.user_timestamp = config.user_time;

            metrics->track_property(Metrics::StringMetric::UserMac, config.user_mac);

            g_metrics_disabled = false;
        }
    }

    bool Metrics::metrics_enabled() { return !g_metrics_disabled; }

    void Metrics::track_metric(const std::string& name, double value) { g_metricmessage.track_metric(name, value); }

    void Metrics::track_buildtime(const std::string& name, double value)
    {
        g_metricmessage.track_buildtime(name, value);
    }

    void Metrics::track_property(DefineMetric metric)
    {
        g_metricmessage.track_property(get_metric_name(metric, get_define_metrics()), "defined");
    }

    void Metrics::track_property(StringMetric metric, const std::string& value)
    {
        g_metricmessage.track_property(get_metric_name(metric, get_string_metrics()), value);
    }

    void Metrics::track_property(BoolMetric metric, bool value)
    {
        g_metricmessage.track_property(get_metric_name(metric, get_bool_metrics()), value);
    }

    void Metrics::track_feature(const std::string& name, bool value) { g_metricmessage.track_feature(name, value); }

    void Metrics::upload(const std::string& payload)
    {
        if (!metrics_enabled())
        {
            return;
        }

#if defined(_WIN32)
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
            if (MAXDWORD <= payload.size()) abort();
            std::wstring hdrs = L"Content-Type: application/json\r\n";
            std::string& p = const_cast<std::string&>(payload);
            results = WinHttpSendRequest(request,
                                         hdrs.c_str(),
                                         static_cast<DWORD>(hdrs.size()),
                                         static_cast<void*>(&p[0]),
                                         static_cast<DWORD>(payload.size()),
                                         static_cast<DWORD>(payload.size()),
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
#else  // ^^^ _WIN32 // !_WIN32 vvv
        (void)payload;
#endif // ^^^ !_WIN32
    }

    void Metrics::flush(Filesystem& fs)
    {
        if (!metrics_enabled())
        {
            return;
        }

        const std::string payload = g_metricmessage.format_event_data_template();
        if (g_should_print_metrics)
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
