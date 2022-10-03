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

#include <mutex>

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
}

namespace vcpkg
{
    template<class T>
    struct LockGuardPtr;

    template<class T>
    struct LockGuarded
    {
        friend struct LockGuardPtr<T>;

    private:
        std::mutex m_mutex;
        T m_t;
    };

    template<class T>
    struct LockGuardPtr
    {
        T& operator*() { return m_ptr; }
        T* operator->() { return &m_ptr; }

        T* get() { return &m_ptr; }

        LockGuardPtr(LockGuarded<T>& sync) : m_lock(sync.m_mutex), m_ptr(sync.m_t) { }

    private:
        std::lock_guard<std::mutex> m_lock;
        T& m_ptr;
    };

    struct Metrics
    {
        Metrics() = default;
        Metrics(const Metrics&) = delete;
        Metrics& operator=(const Metrics&) = delete;

        void track_elapsed_us(double value);
        void track_buildtime(StringView name, double value);

        void track_define_property(DefineMetric metric);
        void track_string_property(StringMetric metric, StringView value);
        void track_bool_property(BoolMetric metric, bool value);

        void track_feature(StringView feature, bool value);

        void flush(Filesystem& fs);
    };

    LockGuarded<Metrics> g_metrics;

    MetricsCollector::MetricsCollector() = default;
    MetricsCollector::~MetricsCollector() = default;

    struct MetricsCollectorImpl : MetricsCollector
    {
        virtual void track_elapsed_us(double value) override
        {
            LockGuardPtr<Metrics>(g_metrics)->track_elapsed_us(value);
        };
        virtual void track_buildtime(StringView name, double value) override
        {
            LockGuardPtr<Metrics>(g_metrics)->track_buildtime(name, value);
        };
        virtual void track_define_property(DefineMetric metric) override
        {
            LockGuardPtr<Metrics>(g_metrics)->track_define_property(metric);
        }
        virtual void track_string_property(StringMetric metric, StringView value) override
        {
            LockGuardPtr<Metrics>(g_metrics)->track_string_property(metric, value);
        }
        virtual void track_bool_property(BoolMetric metric, bool value) override
        {
            LockGuardPtr<Metrics>(g_metrics)->track_bool_property(metric, value);
        }
        virtual void track_feature(StringView feature, bool value) override
        {
            LockGuardPtr<Metrics>(g_metrics)->track_feature(feature, value);
        }
        virtual void track_submission(MetricsSubmission&& submission) override
        {
            LockGuardPtr<Metrics> metrics(g_metrics);
            for (auto&& elapsed_us : submission.elapsed_us)
            {
                metrics->track_elapsed_us(elapsed_us);
            }
            for (auto&& buildtime : submission.buildtimes)
            {
                metrics->track_buildtime(buildtime.first, buildtime.second);
            }
            for (auto&& define_property : submission.define_properties)
            {
                metrics->track_define_property(define_property);
            }
            for (auto&& string_property : submission.string_properties)
            {
                metrics->track_string_property(string_property.first, string_property.second);
            }
            for (auto&& bool_property : submission.bool_properties)
            {
                metrics->track_bool_property(bool_property.first, bool_property.second);
            }
            for (auto&& feature_metric : submission.feature_metrics)
            {
                metrics->track_feature(feature_metric.first, feature_metric.second);
            }
        }
    };

    static MetricsCollectorImpl g_metrics_collector;
    MetricsCollector& get_global_metrics_collector() noexcept { return g_metrics_collector; }

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
        {BoolMetric::InstallManifestMode, "install_manifest_mode"},
        {BoolMetric::OptionOverlayPorts, "option_overlay_ports"},
    }};

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

        void track_string(StringView name, StringView value)
        {
            properties.insert_or_replace(name, Json::Value::string(value));
        }

        void track_bool(StringView name, bool value)
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
            obj.insert("time", Json::Value::string(CTime::now_string()));
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
    std::atomic<bool> g_should_send_metrics =
#if defined(NDEBUG)
        true
#else
        false
#endif
        ;
    std::atomic<bool> g_should_print_metrics = false;
    std::atomic<bool> g_metrics_enabled = false;

    static std::atomic<bool> g_initializing_metrics = false;

    void Metrics::track_elapsed_us(double value) { g_metricmessage.track_metric("elapsed_us", value); }
    void Metrics::track_buildtime(StringView name, double value) { g_metricmessage.track_buildtime(name, value); }

    void Metrics::track_define_property(DefineMetric metric)
    {
        g_metricmessage.track_string(get_metric_name(metric, all_define_metrics), "defined");
    }

    void Metrics::track_string_property(StringMetric metric, StringView value)
    {
        g_metricmessage.track_string(get_metric_name(metric, all_string_metrics), value);
    }

    void Metrics::track_bool_property(BoolMetric metric, bool value)
    {
        g_metricmessage.track_bool(get_metric_name(metric, all_bool_metrics), value);
    }

    void Metrics::track_feature(StringView name, bool value) { g_metricmessage.track_feature(name, value); }

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

    void Metrics::flush(Filesystem& fs)
    {
        if (!g_metrics_enabled.load())
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

    // Must be called outside the g_metrics lock.
    void enable_global_metrics(Filesystem& fs) {
        if (g_initializing_metrics.exchange(true))
        {
            return;
        }

        // Execute this body exactly once
        auto config = try_read_user_config(fs);
        if (config.fill_in_system_values())
        {
            config.try_write(fs);
        }

        {
            LockGuardPtr<Metrics> metrics(g_metrics);
            g_metricmessage.user_id = config.user_id;
            g_metricmessage.user_timestamp = config.user_time;

            metrics->track_string_property(StringMetric::UserMac, config.user_mac);

            g_metrics_enabled = true;
        }
    }
    void flush_global_metrics(Filesystem& fs) { LockGuardPtr<Metrics>(g_metrics)->flush(fs); }
}
