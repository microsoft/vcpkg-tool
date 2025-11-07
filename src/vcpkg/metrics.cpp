#include <vcpkg/base/chrono.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/curl.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.deviceid.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.mac.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/uuid.h>

#include <vcpkg/commands.version.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>

#include <math.h>

#include <iterator>
#include <limits>
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
    constexpr StringLiteral get_metric_name(const T metric, const MetricEntry (&entries)[Size]) noexcept
    {
        auto metric_index = static_cast<size_t>(metric);
        if (metric_index < Size)
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
        const auto n = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
        path.resize(n);
        path += L"\\kernel32.dll";

        const auto versz = GetFileVersionInfoSizeW(path.c_str(), nullptr);
        if (versz == 0) return {};

        std::vector<char> verbuf;
        verbuf.resize(versz);

        if (!GetFileVersionInfoW(path.c_str(), 0, static_cast<DWORD>(verbuf.size()), verbuf.data())) return {};

        void* rootblock;
        UINT rootblocksize;
        if (!VerQueryValueW(verbuf.data(), L"\\", &rootblock, &rootblocksize)) return {};

        auto rootblock_ffi = static_cast<VS_FIXEDFILEINFO*>(rootblock);

        return fmt::format("{}.{}.{}",
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
    // NOTE: New metric names should use `_` instead of `-` to simplify query syntax.
    const constexpr DefineMetricEntry all_define_metrics[static_cast<size_t>(DefineMetric::COUNT)] = {
        {DefineMetric::AssetSource, "asset-source"},
        {DefineMetric::BinaryCachingAws, "binarycaching_aws"},
        {DefineMetric::BinaryCachingAzBlob, "binarycaching_azblob"},
        {DefineMetric::BinaryCachingAzCopy, "binarycaching_azcopy"},
        {DefineMetric::BinaryCachingAzCopySas, "binarycaching_azcopy_sas"},
        {DefineMetric::BinaryCachingCos, "binarycaching_cos"},
        {DefineMetric::BinaryCachingDefault, "binarycaching_default"},
        {DefineMetric::BinaryCachingFiles, "binarycaching_files"},
        {DefineMetric::BinaryCachingGcs, "binarycaching_gcs"},
        {DefineMetric::BinaryCachingHttp, "binarycaching_http"},
        {DefineMetric::BinaryCachingNuGet, "binarycaching_nuget"},
        {DefineMetric::BinaryCachingSource, "binarycaching-source"},
        {DefineMetric::BinaryCachingUpkg, "binarycaching_upkg"},
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
        {DefineMetric::VcpkgNuGetRepository, "VCPKG_NUGET_REPOSITORY"},
        {DefineMetric::VersioningErrorBaseline, "versioning-error-baseline"},
        {DefineMetric::VersioningErrorVersion, "versioning-error-version"},
        {DefineMetric::X_VcpkgRegistriesCache, "X_VCPKG_REGISTRIES_CACHE"},
        {DefineMetric::X_WriteNuGetPackagesConfig, "x-write-nuget-packages-config"},
    };

    // SHA256s separated by colons, separated by commas
    static constexpr char plan_example[] = "0000000011111111aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffff:"
                                           "0000000011111111aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffff:"
                                           "0000000011111111aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffff,"
                                           "0000000011111111aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffff:"
                                           "0000000011111111aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffff:"
                                           "0000000011111111aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffff";

    // NOTE: New metric names should use `_` instead of `-` to simplify query syntax.
    const constexpr StringMetricEntry all_string_metrics[static_cast<size_t>(StringMetric::COUNT)] = {
        // registryUri:id:version,...
        {StringMetric::AcquiredArtifacts, "acquired_artifacts", plan_example},
        {StringMetric::ActivatedArtifacts, "activated_artifacts", plan_example},
        {StringMetric::CiOwnerId, "ci_owner_id", "0"},
        {StringMetric::CiProjectId, "ci_project_id", "0"},
        {StringMetric::CommandArgs, "command_args", "0000000011111111aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffff"},
        {StringMetric::CommandContext, "command_context", "artifact"},
        {StringMetric::CommandName, "command_name", "z-preregister-telemetry"},
        {StringMetric::DeploymentKind, "deployment_kind", "Git"},
        {StringMetric::DetectedCiEnvironment, "detected_ci_environment", "Generic"},
        {StringMetric::DetectedLibCurlVersion, "detected_libcurl_version", "libcurl/8.5.0 OpenSSL/3.0.13"},
        {StringMetric::DevDeviceId, "devdeviceid", "00000000-0000-0000-0000-000000000000"},
        {StringMetric::ExitCode, "exit_code", "0"},
        {StringMetric::ExitLocation,
         "exit_location",
         "0000000011111111aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffff:1"},
        // spec:triplet:version,...
        {StringMetric::InstallPlan_1, "installplan_1", plan_example},
        {StringMetric::ListFile, "listfile", "update to new format"},
        // hashed list of parent process names ;-separated (parent_process;grandparent_process;...)
        {StringMetric::ProcessTree, "process_tree", "0000000011111111aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffff"},
        {StringMetric::RegistriesDefaultRegistryKind, "registries-default-registry-kind", "builtin-files"},
        {StringMetric::RegistriesKindsUsed, "registries-kinds-used", "git,filesystem"},
        {StringMetric::Title, "title", "title"},
        {StringMetric::UserMac, "user_mac", "0"},
        {StringMetric::VcpkgVersion, "vcpkg_version", "2999-12-31-unknownhash"},
        {StringMetric::Warning, "warning", "warning"},
    };

    // NOTE: New metric names should use `_` instead of `-` to simplify query syntax.
    const constexpr BoolMetricEntry all_bool_metrics[static_cast<size_t>(BoolMetric::COUNT)] = {
        {BoolMetric::DetectedContainer, "detected_container"},
        {BoolMetric::DependencyGraphSuccess, "dependency-graph-success"},
        {BoolMetric::FeatureFlagBinaryCaching, "feature-flag-binarycaching"},
        {BoolMetric::FeatureFlagCompilerTracking, "feature-flag-compilertracking"},
        {BoolMetric::FeatureFlagDependencyGraph, "feature-flag-dependency-graph"},
        {BoolMetric::FeatureFlagManifests, "feature-flag-manifests"},
        {BoolMetric::FeatureFlagRegistries, "feature-flag-registries"},
        {BoolMetric::FeatureFlagVersions, "feature-flag-versions"},
        {BoolMetric::InstallManifestMode, "install_manifest_mode"},
        {BoolMetric::OptionOverlayPorts, "option_overlay_ports"},
    };

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
        std::lock_guard<std::mutex> lock{mtx};
        return submission;
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

    std::string MetricsUserConfig::to_string() const { return adapt_to_string(*this); }

    void MetricsUserConfig::try_write(const Filesystem& fs) const
    {
        const auto& maybe_user_dir = get_user_configuration_home();
        if (auto p_user_dir = maybe_user_dir.get())
        {
            fs.create_directory(*p_user_dir, IgnoreErrors{});
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

    MetricsUserConfig try_read_metrics_user(const ReadOnlyFilesystem& fs)
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

        std::vector<std::string> process_list;
        get_parent_process_list(process_list);
        result.parent_process_list = Strings::join(
            ";", process_list, [](auto&& s) { return Hash::get_string_sha256(Strings::ascii_to_lowercase(s)); });

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

            properties.insert("buildnames_1", std::move(buildtime_names));
            properties.insert("buildtimes", std::move(buildtime_times));
        }

        Json::Object& measurements = base_data.insert("measurements", Json::Object());
        if (submission.elapsed_us != 0.0)
        {
            measurements.insert_or_replace("elapsed_us", Json::Value::number(submission.elapsed_us));
        }

        properties.insert_or_replace(get_metric_name(StringMetric::ProcessTree, all_string_metrics),
                                     Json::Value::string(session.parent_process_list));

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

    void flush_global_metrics(const Filesystem& fs)
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

        auto deviceid = get_device_id(fs);
        submission.track_string(StringMetric::DevDeviceId, deviceid);

        const std::string payload = format_metrics_payload(user, session, submission);
        if (g_should_print_metrics.load())
        {
            fprintf(stderr, "%s\n", payload.c_str());
        }

        if (!g_should_send_metrics)
        {
            return;
        }

        const Path temp_folder_path = fs.create_or_get_temp_directory(VCPKG_LINE_INFO);
        const Path vcpkg_metrics_txt_path = temp_folder_path / ("vcpkg" + generate_random_UUID() + ".txt");
        Debug::println("Uploading metrics ", vcpkg_metrics_txt_path);
        std::error_code ec;
        fs.write_contents(vcpkg_metrics_txt_path, payload, ec);
        if (ec) return;

        const Path temp_folder_path_exe = temp_folder_path / "vcpkg-" VCPKG_BASE_VERSION_AS_STRING
#if defined(WIN32)
                                                             ".exe"
#endif
            ;
        fs.copy_file(get_exe_path_of_current_process(), temp_folder_path_exe, CopyOptions::skip_existing, ec);
        if (ec) return;

        Command builder;
        builder.string_arg(temp_folder_path_exe);
        builder.string_arg("z-upload-metrics");
        builder.string_arg(vcpkg_metrics_txt_path);
        cmd_execute_background(builder);
    }

    static size_t string_append_cb(void* buff, size_t size, size_t nmemb, void* param)
    {
        auto* str = reinterpret_cast<std::string*>(param);
        if (!str || !buff) return 0;
        if (size != 1) return 0;
        str->append(reinterpret_cast<char*>(buff), nmemb);
        return size * nmemb;
    }

    bool parse_metrics_response(StringView response_body)
    {
        auto maybe_json = Json::parse_object(response_body, "metrics_response");
        auto json = maybe_json.get();
        if (!json) return false;

        auto maybe_received = json->get(vcpkg::AppInsightsResponseItemsReceived);
        auto maybe_accepted = json->get(vcpkg::AppInsightsResponseItemsAccepted);
        auto maybe_errors = json->get(vcpkg::AppInsightsResponseErrors);

        if (maybe_received && maybe_accepted && maybe_errors && maybe_received->is_integer() &&
            maybe_accepted->is_integer() && maybe_errors->is_array())
        {
            auto item_received = maybe_received->integer(VCPKG_LINE_INFO);
            auto item_accepted = maybe_accepted->integer(VCPKG_LINE_INFO);
            auto errors = maybe_errors->array(VCPKG_LINE_INFO);
            return (errors.size() == 0) && (item_received == item_accepted);
        }
        Debug::println("Metrics response has unexpected format");
        return false;
    }

    bool curl_upload_metrics(const std::string& payload)
    {
        if (payload.length() > static_cast<size_t>(std::numeric_limits<long>::max()))
        {
            Debug::println("Metrics payload too large to upload");
            return false;
        }

        CurlEasyHandle handle;
        CURL* curl = handle.get();

        std::string headers[] = {
            "Content-Type: application/json",
        };
        CurlHeaders request_headers(headers);

        curl_easy_setopt(curl, CURLOPT_URL, "https://dc.services.visualstudio.com/v2/track");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.length()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers.get());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // follow redirects
        curl_easy_setopt(curl, CURLOPT_USERAGENT, vcpkg_curl_user_agent);

        std::string buff;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&buff));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &string_append_cb);

        long response_code = 0;
        CURLcode res = curl_easy_perform(curl);
        bool is_success = false;
        if (res == CURLE_OK)
        {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            Debug::println(fmt::format("Metrics upload response code: {}", response_code));
            Debug::println("Metrics upload response body: ", buff);
            if (response_code == 200)
            {
                is_success = parse_metrics_response(buff);
            }
        }
        else
        {
            Debug::println("Metrics upload failed: ", curl_easy_strerror(res));
        }
        return is_success;
    }
}
