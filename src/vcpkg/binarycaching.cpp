#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/api-stable-format.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/archives.h>
#include <vcpkg/binarycaching.h>
#include <vcpkg/binarycaching.private.h>
#include <vcpkg/build.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgpaths.h>

#include <iterator>

using namespace vcpkg;

namespace
{
    struct ConfigSegmentsParser : ParserBase
    {
        using ParserBase::ParserBase;

        void parse_segments(std::vector<std::pair<SourceLoc, std::string>>& out_segments);
        std::vector<std::vector<std::pair<SourceLoc, std::string>>> parse_all_segments();

        template<class T>
        void handle_readwrite(std::vector<T>& read,
                              std::vector<T>& write,
                              T&& t,
                              const std::vector<std::pair<SourceLoc, std::string>>& segments,
                              size_t segment_idx)
        {
            if (segment_idx >= segments.size())
            {
                read.push_back(std::move(t));
                return;
            }

            auto& mode = segments[segment_idx].second;

            if (mode == "read")
            {
                read.push_back(std::move(t));
            }
            else if (mode == "write")
            {
                write.push_back(std::move(t));
            }
            else if (mode == "readwrite")
            {
                read.push_back(t);
                write.push_back(std::move(t));
            }
            else
            {
                return add_error(msg::format(msgExpectedReadWriteReadWrite), segments[segment_idx].first);
            }
        }

        Optional<IObjectProvider::Access> parse_readwrite(
            const std::vector<std::pair<SourceLoc, std::string>>& segments, size_t segment_idx)
        {
            using Access = IObjectProvider::Access;
            if (segment_idx >= segments.size())
            {
                return Access::Read;
            }

            auto& mode = segments[segment_idx].second;

            if (mode == "read")
            {
                return Access::Read;
            }
            else if (mode == "write")
            {
                return Access::Write;
            }
            else if (mode == "readwrite")
            {
                return Access::ReadWrite;
            }
            else
            {
                add_error(msg::format(msgExpectedReadWriteReadWrite), segments[segment_idx].first);
                return nullopt;
            }
        }
    };

    void ConfigSegmentsParser::parse_segments(std::vector<std::pair<SourceLoc, std::string>>& segments)
    {
        for (;;)
        {
            SourceLoc loc = cur_loc();
            std::string segment;
            for (;;)
            {
                auto n = match_until([](char32_t ch) { return ch == ',' || ch == '`' || ch == ';'; });
                Strings::append(segment, n);
                auto ch = cur();
                if (ch == Unicode::end_of_file || ch == ',' || ch == ';')
                {
                    break;
                }

                if (ch == '`')
                {
                    ch = next();
                    if (ch == Unicode::end_of_file)
                    {
                        return add_error(msg::format(msgUnexpectedEOFAfterBacktick));
                    }
                    else
                    {
                        Unicode::utf8_append_code_point(segment, ch);
                    }

                    next();
                }
                else
                {
                    Checks::unreachable(VCPKG_LINE_INFO);
                }
            }
            segments.emplace_back(loc, std::move(segment));

            auto ch = cur();
            if (ch == Unicode::end_of_file || ch == ';')
            {
                break;
            }

            if (ch == ',')
            {
                next();
                continue;
            }

            Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    std::vector<std::vector<std::pair<SourceLoc, std::string>>> ConfigSegmentsParser::parse_all_segments()
    {
        std::vector<std::vector<std::pair<SourceLoc, std::string>>> ret;
        while (!at_eof())
        {
            std::vector<std::pair<SourceLoc, std::string>> segments;
            parse_segments(segments);

            if (get_error())
            {
                return {};
            }

            // Skip empty sources like ';;'
            if (segments.size() > 1 || (segments.size() == 1 && !segments[0].second.empty()))
            {
                ret.push_back(std::move(segments));
            }

            if (cur() == ';')
            {
                next();
            }
        }
        return ret;
    }

    static const std::string& get_nuget_prefix()
    {
        static std::string nuget_prefix = []() {
            auto x = get_environment_variable("X_VCPKG_NUGET_ID_PREFIX").value_or("");
            if (!x.empty())
            {
                x.push_back('_');
            }
            return x;
        }();
        return nuget_prefix;
    }

    static void clean_prepare_dir(Filesystem& fs, const Path& dir)
    {
        fs.remove_all(dir, VCPKG_LINE_INFO);
        if (!fs.create_directories(dir, VCPKG_LINE_INFO))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgUnableToClearPath, msg::path = dir);
        }
    }

    static Path make_temp_archive_path(const Path& buildtrees, const PackageSpec& spec)
    {
        return buildtrees / spec.name() / (spec.triplet().to_string() + ".zip");
    }

    struct ISingleObjectProvider : IObjectProvider
    {
        using IObjectProvider::IObjectProvider;
        virtual ~ISingleObjectProvider() = default;

        void download(Optional<const ToolCache&> tool_cache,
                      View<StringView> objects,
                      const Path& target_dir) const override final
        {
            for (auto object : objects)
            {
                download(Optional<const ToolCache&>(tool_cache), object, target_dir / object);
            }
        };
        virtual void download(Optional<const ToolCache&> tool_cache,
                              StringView object,
                              const Path& target_file) const = 0;

        void check_availability(Optional<const ToolCache&> tool_cache,
                                View<StringView> objects,
                                Span<bool> cache_status) const override final
        {
            auto iter = cache_status.begin();
            for (auto object : objects)
            {
                *iter = is_available(tool_cache, object);
                ++iter;
            }
        }
        virtual bool is_available(Optional<const ToolCache&> tool_cache, StringView object) const = 0;
    };

    struct FileObjectProvider : ISingleObjectProvider
    {
        FileObjectProvider(Access access, Filesystem& filesystem, Path&& dir)
            : ISingleObjectProvider(access), fs(filesystem), m_dir(std::move(dir))
        {
            fs.create_directories(m_dir, VCPKG_LINE_INFO);
        }
        static Path make_archive_subpath(const StringView abi) { return Path(abi.substr(0, 2)) / abi; }
        void download(Optional<const ToolCache&>, StringView object, const Path& target_file) const override
        {
            const auto archive_path = m_dir / make_archive_subpath(object);
            if (fs.exists(archive_path, IgnoreErrors{}))
            {
                fs.copy_file(archive_path, target_file, CopyOptions::overwrite_existing, VCPKG_LINE_INFO);
            }
        }
        void upload(Optional<const ToolCache&>, StringView object_id, const Path& object, MessageSink&) override
        {
            const auto archive_subpath = make_archive_subpath(object_id);
            const auto archive_path = m_dir / archive_subpath;
            fs.create_directories(archive_path.parent_path(), IgnoreErrors{});
            fs.copy_file(object, archive_path, CopyOptions::overwrite_existing, VCPKG_LINE_INFO);
        }
        bool is_available(Optional<const ToolCache&>, StringView object) const override
        {
            const auto archive_path = m_dir / make_archive_subpath(object);
            return fs.exists(archive_path, IgnoreErrors{});
        }

    private:
        Filesystem& fs;
        Path m_dir;
    };

    struct HttpObjectProvider : IObjectProvider
    {
        HttpObjectProvider(Access access,
                           Filesystem& fs,
                           UrlTemplate&& url_template,
                           const std::vector<std::string>& secrets)
            : IObjectProvider(access), fs(fs), m_url_template(std::move(url_template)), m_secrets(secrets)
        {
        }

        void download(Optional<const ToolCache&>, View<StringView> objects, const Path& target_dir) const override
        {
            auto url_paths = Util::fmap(objects, [&](StringView object) -> std::pair<std::string, Path> {
                return {m_url_template.instantiate_variable(object), target_dir / object};
            });
            auto codes = download_files(fs, url_paths, m_url_template.headers_for_get);
            for (size_t i = 0; i < codes.size(); ++i)
            {
                if (codes[i] != 200)
                {
                    fs.remove(url_paths[i].second, IgnoreErrors{});
                }
            }
        }
        void upload(Optional<const ToolCache&>,
                    StringView object_id,
                    const Path& object,
                    MessageSink& msg_sink) override
        {
            auto maybe_success = put_file(
                fs, m_url_template.instantiate_variable(object_id), m_secrets, m_url_template.headers_for_put, object);
            if (!maybe_success)
            {
                msg_sink.println(Color::warning, maybe_success.error());
            }
        }
        void check_availability(Optional<const ToolCache&>,
                                vcpkg::View<StringView> objects,
                                vcpkg::Span<bool> cache_status) const override
        {
            auto urls = Util::fmap(objects, [&](StringView object) -> std::string {
                return m_url_template.url_template + object.to_string();
            });
            auto codes = url_heads(urls, {}, m_secrets);
            Checks::check_exit(VCPKG_LINE_INFO, codes.size() == urls.size());
            for (size_t i = 0; i < codes.size(); ++i)
            {
                cache_status[i] = (codes[i] == 200);
            }
        }

        Filesystem& fs;
        UrlTemplate m_url_template;
        std::vector<std::string> m_secrets;
    };
    struct NugetBinaryProvider : IBinaryProvider
    {
        NugetBinaryProvider(const VcpkgPaths& paths,
                            std::vector<std::string>&& read_sources,
                            std::vector<std::string>&& write_sources,
                            std::vector<Path>&& read_configs,
                            std::vector<Path>&& write_configs,
                            std::string&& timeout,
                            bool nuget_interactive)
            : paths(paths)
            , m_read_sources(std::move(read_sources))
            , m_write_sources(std::move(write_sources))
            , m_read_configs(std::move(read_configs))
            , m_write_configs(std::move(write_configs))
            , m_timeout(std::move(timeout))
            , m_interactive(nuget_interactive)
            , m_use_nuget_cache(false)
        {
            const std::string use_nuget_cache = get_environment_variable("VCPKG_USE_NUGET_CACHE").value_or("");
            m_use_nuget_cache =
                Strings::case_insensitive_ascii_equals(use_nuget_cache, "true") || use_nuget_cache == "1";
        }

        ExpectedL<Unit> run_nuget_commandline(const Command& cmdline, MessageSink& msg_sink) const
        {
            if (m_interactive)
            {
                return cmd_execute(cmdline).then([](int exit_code) -> ExpectedL<Unit> {
                    if (exit_code == 0)
                    {
                        return {Unit{}};
                    }

                    return msg::format_error(msgNugetOutputNotCapturedBecauseInteractiveSpecified);
                });
            }

            return cmd_execute_and_capture_output(cmdline).then([&](ExitCodeAndOutput&& res) -> ExpectedL<Unit> {
                if (Debug::g_debugging)
                {
                    msg_sink.print(Color::error, res.output);
                }

                if (res.output.find("Authentication may require manual action.") != std::string::npos)
                {
                    msg_sink.println(Color::warning, msgAuthenticationMayRequireManualAction, msg::vendor = "Nuget");
                }

                if (res.exit_code == 0)
                {
                    return {Unit{}};
                }

                if (res.output.find("Response status code does not indicate success: 401 (Unauthorized)") !=
                    std::string::npos)
                {
                    msg_sink.println(Color::warning,
                                     msgFailedVendorAuthentication,
                                     msg::vendor = "NuGet",
                                     msg::url = docs::binarycaching_url);
                }
                else if (res.output.find("for example \"-ApiKey AzureDevOps\"") != std::string::npos)
                {
                    auto real_cmdline = cmdline;
                    real_cmdline.string_arg("-ApiKey").string_arg("AzureDevOps");
                    return cmd_execute_and_capture_output(real_cmdline)
                        .then([&](ExitCodeAndOutput&& res) -> ExpectedL<Unit> {
                            if (Debug::g_debugging)
                            {
                                msg_sink.print(Color::error, res.output);
                            }

                            if (res.exit_code == 0)
                            {
                                return {Unit{}};
                            }

                            return LocalizedString::from_raw(std::move(res).output);
                        });
                }

                return LocalizedString::from_raw(std::move(res).output);
            });
        }

        struct NuGetPrefetchAttempt
        {
            PackageSpec spec;
            NugetReference reference;
            size_t result_index;
        };

        static void generate_packages_config(Filesystem& fs,
                                             const Path& packages_config,
                                             const std::vector<NuGetPrefetchAttempt>& attempts)
        {
            XmlSerializer xml;
            xml.emit_declaration().line_break();
            xml.open_tag("packages").line_break();

            for (auto&& attempt : attempts)
            {
                xml.start_complex_open_tag("package")
                    .text_attr("id", attempt.reference.id)
                    .text_attr("version", attempt.reference.version)
                    .finish_self_closing_complex_tag()
                    .line_break();
            }

            xml.close_tag("packages").line_break();
            fs.write_contents(packages_config, xml.buf, VCPKG_LINE_INFO);
        }

        void prefetch(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const override
        {
            if (m_read_sources.empty() && m_read_configs.empty())
            {
                return;
            }

            const ElapsedTimer timer;
            auto& fs = paths.get_filesystem();

            std::vector<NuGetPrefetchAttempt> attempts;
            for (size_t idx = 0; idx < cache_status.size(); ++idx)
            {
                auto this_cache_status = cache_status[idx];
                if (!this_cache_status || !this_cache_status->should_attempt_restore(this))
                {
                    continue;
                }

                const auto& action = actions[idx];
                const auto& spec = action.spec;
                fs.remove_all(paths.package_dir(spec), VCPKG_LINE_INFO);
                attempts.push_back({spec, make_nugetref(action, get_nuget_prefix()), idx});
            }

            if (attempts.empty())
            {
                return;
            }

            msg::println(msgAttemptingToFetchPackagesFromVendor, msg::count = attempts.size(), msg::vendor = "nuget");

            auto packages_config = paths.buildtrees() / "packages.config";
            const auto& nuget_exe = paths.get_tool_exe("nuget", stdout_sink);
            std::vector<Command> cmdlines;

            if (!m_read_sources.empty())
            {
                // First check using all sources
                Command cmdline;
#ifndef _WIN32
                cmdline.string_arg(paths.get_tool_exe(Tools::MONO, stdout_sink));
#endif
                cmdline.string_arg(nuget_exe)
                    .string_arg("install")
                    .string_arg(packages_config)
                    .string_arg("-OutputDirectory")
                    .string_arg(paths.packages())
                    .string_arg("-Source")
                    .string_arg(Strings::join(";", m_read_sources))
                    .string_arg("-ExcludeVersion")
                    .string_arg("-PreRelease")
                    .string_arg("-PackageSaveMode")
                    .string_arg("nupkg")
                    .string_arg("-Verbosity")
                    .string_arg("detailed")
                    .string_arg("-ForceEnglishOutput");
                if (!m_interactive)
                {
                    cmdline.string_arg("-NonInteractive");
                }
                if (!m_use_nuget_cache)
                {
                    cmdline.string_arg("-DirectDownload").string_arg("-NoCache");
                }

                cmdlines.push_back(std::move(cmdline));
            }

            for (auto&& cfg : m_read_configs)
            {
                // Then check using each config
                Command cmdline;
#ifndef _WIN32
                cmdline.string_arg(paths.get_tool_exe(Tools::MONO, stdout_sink));
#endif
                cmdline.string_arg(nuget_exe)
                    .string_arg("install")
                    .string_arg(packages_config)
                    .string_arg("-OutputDirectory")
                    .string_arg(paths.packages())
                    .string_arg("-ConfigFile")
                    .string_arg(cfg)
                    .string_arg("-ExcludeVersion")
                    .string_arg("-PreRelease")
                    .string_arg("-PackageSaveMode")
                    .string_arg("nupkg")
                    .string_arg("-Verbosity")
                    .string_arg("detailed")
                    .string_arg("-ForceEnglishOutput");
                if (!m_interactive)
                {
                    cmdline.string_arg("-NonInteractive");
                }
                if (!m_use_nuget_cache)
                {
                    cmdline.string_arg("-DirectDownload").string_arg("-NoCache");
                }

                cmdlines.push_back(std::move(cmdline));
            }

            const size_t total_restore_attempts = attempts.size();
            for (const auto& cmdline : cmdlines)
            {
                if (attempts.empty())
                {
                    break;
                }

                generate_packages_config(fs, packages_config, attempts);
                run_nuget_commandline(cmdline, stdout_sink);
                Util::erase_remove_if(attempts, [&](const NuGetPrefetchAttempt& nuget_ref) -> bool {
                    // note that we would like the nupkg downloaded to buildtrees, but nuget.exe downloads it to the
                    // output directory
                    const auto nupkg_path =
                        paths.packages() / nuget_ref.reference.id / nuget_ref.reference.id + ".nupkg";
                    if (fs.exists(nupkg_path, IgnoreErrors{}))
                    {
                        fs.remove(nupkg_path, VCPKG_LINE_INFO);
                        const auto nuget_dir = nuget_ref.spec.dir();
                        if (nuget_dir != nuget_ref.reference.id)
                        {
                            const auto path_from = paths.packages() / nuget_ref.reference.id;
                            const auto path_to = paths.packages() / nuget_dir;
                            fs.rename(path_from, path_to, VCPKG_LINE_INFO);
                        }

                        cache_status[nuget_ref.result_index]->mark_restored();
                        return true;
                    }

                    return false;
                });
            }
            msg::println(msgRestoredPackagesFromVendor,
                         msg::count = total_restore_attempts - attempts.size(),
                         msg::elapsed = timer.elapsed(),
                         msg::value = "NuGet");
        }

        RestoreResult try_restore(const InstallPlanAction&) const override { return RestoreResult::unavailable; }

        bool needs_nuspec_data() const override { return !m_write_sources.empty() || !m_write_configs.empty(); }

        void push_success(const BinaryProviderPushRequest& request, MessageSink& msg_sink) override
        {
            if (m_write_sources.empty() && m_write_configs.empty())
            {
                return;
            }
            if (request.info.nuspec.empty())
            {
                Checks::unreachable(
                    VCPKG_LINE_INFO,
                    "request.info.nuspec must be non empty because needs_nuspec_data() should return true");
            }

            auto& spec = request.info.spec;

            NugetReference nuget_ref = make_nugetref(request.info, get_nuget_prefix());
            auto nuspec_path = paths.buildtrees() / spec.name() / (spec.triplet().to_string() + ".nuspec");
            auto& fs = paths.get_filesystem();
            fs.write_contents(nuspec_path, request.info.nuspec, VCPKG_LINE_INFO);

            const auto& nuget_exe = paths.get_tool_exe("nuget", stdout_sink);
            Command cmdline;
#ifndef _WIN32
            cmdline.string_arg(paths.get_tool_exe(Tools::MONO, stdout_sink));
#endif
            cmdline.string_arg(nuget_exe)
                .string_arg("pack")
                .string_arg(nuspec_path)
                .string_arg("-OutputDirectory")
                .string_arg(paths.buildtrees())
                .string_arg("-NoDefaultExcludes")
                .string_arg("-ForceEnglishOutput");

            if (!m_interactive)
            {
                cmdline.string_arg("-NonInteractive");
            }

            if (!run_nuget_commandline(cmdline, msg_sink))
            {
                msg_sink.println(Color::error, msgPackingVendorFailed, msg::vendor = "NuGet");
                return;
            }

            auto nupkg_path = paths.buildtrees() / nuget_ref.nupkg_filename();
            for (auto&& write_src : m_write_sources)
            {
                Command cmd;
#ifndef _WIN32
                cmd.string_arg(paths.get_tool_exe(Tools::MONO, stdout_sink));
#endif
                cmd.string_arg(nuget_exe)
                    .string_arg("push")
                    .string_arg(nupkg_path)
                    .string_arg("-ForceEnglishOutput")
                    .string_arg("-Timeout")
                    .string_arg(m_timeout)
                    .string_arg("-Source")
                    .string_arg(write_src);

                if (!m_interactive)
                {
                    cmd.string_arg("-NonInteractive");
                }
                msg_sink.println(
                    msgUploadingBinariesToVendor, msg::spec = spec, msg::vendor = "NuGet", msg::path = write_src);
                if (!run_nuget_commandline(cmd, msg_sink))
                {
                    msg_sink.println(
                        Color::error, msgPushingVendorFailed, msg::vendor = "NuGet", msg::path = write_src);
                }
            }
            for (auto&& write_cfg : m_write_configs)
            {
                Command cmd;
#ifndef _WIN32
                cmd.string_arg(paths.get_tool_exe(Tools::MONO, msg_sink));
#endif
                cmd.string_arg(nuget_exe)
                    .string_arg("push")
                    .string_arg(nupkg_path)
                    .string_arg("-ForceEnglishOutput")
                    .string_arg("-Timeout")
                    .string_arg(m_timeout)
                    .string_arg("-ConfigFile")
                    .string_arg(write_cfg);
                if (!m_interactive)
                {
                    cmd.string_arg("-NonInteractive");
                }
                msg_sink.println(Color::error,
                                 msgUploadingBinariesUsingVendor,
                                 msg::spec = spec,
                                 msg::vendor = "NuGet config",
                                 msg::path = write_cfg);
                if (!run_nuget_commandline(cmd, msg_sink))
                {
                    msg_sink.println(
                        Color::error, msgPushingVendorFailed, msg::vendor = "NuGet", msg::path = write_cfg);
                }
            }

            fs.remove(nupkg_path, IgnoreErrors{});
        }

        void precheck(View<InstallPlanAction>, View<CacheStatus*>) const override { }

    private:
        const VcpkgPaths& paths;

        std::vector<std::string> m_read_sources;
        std::vector<std::string> m_write_sources;

        std::vector<Path> m_read_configs;
        std::vector<Path> m_write_configs;

        std::string m_timeout;
        bool m_interactive;
        bool m_use_nuget_cache;
    };

    struct BinaryObjectProvider : IBinaryProvider
    {
        BinaryObjectProvider(const VcpkgPaths& paths,
                             std::vector<std::unique_ptr<IObjectProvider>>&& providers,
                             std::vector<ExtendedUrlTemplate>&& put_url_templates,
                             const std::vector<std::string>& secrets)
            : paths(paths)
            , m_providers(std::move(providers))
            , m_put_url_templates(std::move(put_url_templates))
            , m_secrets(std::move(secrets))
        {
        }

        static std::string make_object_id(const std::string& abi) { return Strings::concat(abi, ".zip"); }

        void prefetch(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const override
        {
            auto& fs = paths.get_filesystem();

            const ElapsedTimer timer;
            size_t restored_count = 0;
            std::vector<std::string> objects;
            for (const auto& provider : m_providers)
            {
                if (!provider->supports_read()) continue;
                objects.clear();
                std::vector<size_t> object_indices;

                for (size_t idx = 0; idx < cache_status.size(); ++idx)
                {
                    const auto this_cache_status = cache_status[idx];
                    if (!this_cache_status || !this_cache_status->should_attempt_restore(this))
                    {
                        continue;
                    }

                    auto&& action = actions[idx];
                    objects.push_back(make_object_id(action.package_abi().value_or_exit(VCPKG_LINE_INFO)));
                    object_indices.push_back(idx);
                }

                if (objects.empty()) break;

                msg::println(
                    msgAttemptingToFetchPackagesFromVendor, msg::count = objects.size(), msg::vendor = "vendor()");
                const auto objects_view = Util::fmap(objects, [](std::string& s) -> StringView { return s; });
                Path target_dir = paths.downloads / "binary_cache";
                fs.create_directories(target_dir, VCPKG_LINE_INFO);
                provider->download(paths.get_tool_cache(), objects_view, target_dir);
                std::vector<Command> jobs;
                std::vector<size_t> idxs;
                for (size_t idx = 0; idx < objects.size(); ++idx)
                {
                    Path object_path = target_dir / objects[idx];
                    if (!fs.exists(object_path, IgnoreErrors{})) continue;
                    auto pkg_path = paths.package_dir(actions[object_indices[idx]].spec);
                    clean_prepare_dir(fs, pkg_path);
                    jobs.push_back(
                        decompress_zip_archive_cmd(paths.get_tool_cache(), stdout_sink, pkg_path, object_path));
                    idxs.push_back(idx);
                }

                const auto job_results =
                    cmd_execute_and_capture_output_parallel(jobs, default_working_directory, get_clean_environment());

                for (size_t j = 0; j < jobs.size(); ++j)
                {
                    const auto idx = idxs[j];
                    if (!job_results[j])
                    {
                        Debug::print("Failed to decompress ", target_dir / objects[idx], '\n');
                        continue;
                    }

                    // decompression success
                    ++restored_count;
                    fs.remove(target_dir / objects[idx], VCPKG_LINE_INFO);
                    cache_status[object_indices[idx]]->mark_restored();
                }
            }

            msg::println(msgRestoredPackagesFromVendor,
                         msg::count = restored_count,
                         msg::elapsed = timer.elapsed(),
                         msg::value = "vendor()");
        }

        RestoreResult try_restore(const InstallPlanAction& action) const override
        {
            CacheStatus status;
            CacheStatus* pointer = &status;
            prefetch({&action, 1}, {&pointer, 1});
            return status.is_restored() ? RestoreResult::restored : RestoreResult::unavailable;
        }

        void push_success(const BinaryProviderPushRequest& request, MessageSink& msg_sink) override
        {
            const ElapsedTimer timer;
            const auto& abi = request.info.package_abi;
            auto& spec = request.info.spec;
            const auto tmp_archive_path = make_temp_archive_path(paths.buildtrees(), spec);
            auto compression_result = compress_directory_to_zip(
                paths.get_filesystem(), paths.get_tool_cache(), msg_sink, request.package_dir, tmp_archive_path);
            if (!compression_result)
            {
                msg_sink.println(Color::warning,
                                 msg::format_warning(msgCompressFolderFailed, msg::path = request.package_dir)
                                     .append_raw(' ')
                                     .append_raw(compression_result.error()));
                return;
            }
            const auto object_id = make_object_id(abi);
            size_t upload_count = 0;
            for (auto& provider : m_providers)
            {
                if (provider->supports_write())
                {
                    provider->upload(paths.get_tool_cache(), object_id, tmp_archive_path, msg_sink);
                }
            }
            for (auto&& put_url_template : m_put_url_templates)
            {
                auto url = put_url_template.instantiate_variables(request.info);
                auto maybe_success = put_file(
                    paths.get_filesystem(), url, m_secrets, put_url_template.headers_for_put, tmp_archive_path);
                if (maybe_success)
                {
                    continue;
                }

                msg_sink.println(Color::warning, maybe_success.error());
            }
            msg_sink.println(msgUploadedPackagesToVendor,
                             msg::count = upload_count,
                             msg::elapsed = timer.elapsed(),
                             msg::vendor = "vendor()");
        }

        void precheck(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const override
        {
            std::vector<CacheAvailability> actions_availability{actions.size(), CacheAvailability::unavailable};
            std::vector<std::string> object_names{actions.size()};
            std::vector<StringView> object_ids;
            std::vector<size_t> idxs;
            for (auto& provider : m_providers)
            {
                if (!provider->supports_read()) continue;
                idxs.clear();
                object_ids.clear();
                for (size_t idx = 0; idx < actions.size(); ++idx)
                {
                    auto&& action = actions[idx];
                    const auto& abi = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
                    if (!cache_status[idx]->should_attempt_precheck(this))
                    {
                        continue;
                    }
                    if (object_names[idx].empty())
                    {
                        object_names[idx] = make_object_id(abi);
                    }
                    object_ids.push_back(object_names[idx]);
                    idxs.push_back(idx);
                }
                std::vector<int8_t> status(object_ids.size(), 0);
                Span<bool> bool_span(reinterpret_cast<bool*>(status.data()), status.size());
                provider->check_availability(paths.get_tool_cache(), object_ids, bool_span);
                for (size_t idx = 0; idx < bool_span.size(); ++idx)
                {
                    if (bool_span[idx])
                    {
                        auto global_idx = idxs[idx];
                        actions_availability[global_idx] = CacheAvailability::available;
                        cache_status[global_idx]->mark_available(this);
                    }
                }
            }

            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                const auto this_cache_status = cache_status[idx];
                if (this_cache_status && actions_availability[idx] == CacheAvailability::unavailable)
                {
                    this_cache_status->mark_unavailable(this);
                }
            }
        }

    private:
        const VcpkgPaths& paths;
        std::vector<std::unique_ptr<IObjectProvider>> m_providers;
        std::vector<ExtendedUrlTemplate> m_put_url_templates;
        std::vector<ExtendedUrlTemplate> m_get_url_templates;
        std::vector<std::string> m_secrets;
    };

    struct GHABinaryProvider : ISingleObjectProvider
    {
        GHABinaryProvider(Access access, Filesystem& fs, const std::string& url, const std::string& token)
            : ISingleObjectProvider(access), fs(fs)
        {
            m_read_url = url + "_apis/artifactcache/cache";
            m_write_url = url + "_apis/artifactcache/caches";
            m_token_header = "Authorization: Bearer " + token;
        }

        Command command() const
        {
            Command cmd;
            cmd.string_arg("curl")
                .string_arg("-s")
                .string_arg("-H")
                .string_arg("Content-Type: application/json")
                .string_arg("-H")
                .string_arg(m_token_header)
                .string_arg("-H")
                .string_arg(m_accept_header);
            return cmd;
        }

        std::string lookup_cache_entry(StringView abi) const
        {
            auto cmd = command()
                           .string_arg(m_read_url)
                           .string_arg("-G")
                           .string_arg("-d")
                           .string_arg("keys=vcpkg")
                           .string_arg("-d")
                           .string_arg("version=" + abi);

            std::vector<std::string> lines;
            auto res = cmd_execute_and_capture_output(cmd);
            if (!res.has_value() || res.get()->exit_code) return {};
            auto json = Json::parse_object(res.get()->output);
            if (!json.has_value() || !json.get()->contains("archiveLocation")) return {};
            return json.get()->get("archiveLocation")->string(VCPKG_LINE_INFO).to_string();
        }

        Optional<int64_t> reserve_cache_entry(StringView abi, int64_t cacheSize) const
        {
            Json::Object payload;
            payload.insert("key", "vcpkg");
            payload.insert("version", abi);
            payload.insert("cacheSize", Json::Value::integer(cacheSize));
            auto cmd = command().string_arg(m_write_url).string_arg("-d").string_arg(stringify(payload));

            auto res = cmd_execute_and_capture_output(cmd);
            if (!res.has_value() || res.get()->exit_code) return {};
            auto json = Json::parse_object(res.get()->output);
            if (!json.has_value() || !json.get()->contains("cacheId")) return {};
            return json.get()->get("cacheId")->integer(VCPKG_LINE_INFO);
        }

        void upload(vcpkg::Optional<const ToolCache&>,
                    StringView object_id,
                    const Path& object_file,
                    MessageSink& msg_sink) override
        {
            int64_t cache_size;
            {
                auto archive = fs.open_for_read(object_file, VCPKG_LINE_INFO);
                archive.try_seek_to(0, SEEK_END);
                cache_size = archive.tell();
            }
            if (auto cacheId = reserve_cache_entry(object_id, cache_size))
            {
                std::vector<std::string> headers{
                    m_token_header,
                    m_accept_header.to_string(),
                    "Content-Type: application/octet-stream",
                    "Content-Range: bytes 0-" + std::to_string(cache_size) + "/*",
                };
                auto url = m_write_url + "/" + std::to_string(*cacheId.get());
                if (put_file(fs, url, {}, headers, object_file, "PATCH"))
                {
                    Json::Object commit;
                    commit.insert("size", std::to_string(cache_size));
                    auto cmd = command().string_arg(url).string_arg("-d").string_arg(stringify(commit));

                    auto res = flatten_out(cmd_execute_and_capture_output(cmd), "curl");
                    if (!res)
                    {
                        msg_sink.println_error(res.error());
                    }
                }
            }
        }

        void download(vcpkg::Optional<const ToolCache&>, StringView object, const Path& target_file) const override
        {
            auto url = lookup_cache_entry(object);
            if (!url.empty())
            {
                std::pair<std::string, Path> p(url, target_file);
                download_files(fs, {&p, 1}, {});
            }
        }
        bool is_available(vcpkg::Optional<const ToolCache&>, StringView object) const override
        {
            return !lookup_cache_entry(object).empty();
        }

        static constexpr StringLiteral m_accept_header = "Accept: application/json;api-version=6.0-preview.1";
        std::string m_token_header;

        Filesystem& fs;
        std::string m_read_url;
        std::string m_write_url;
    };

    struct GcsObjectProvider : ISingleObjectProvider
    {
        GcsObjectProvider(Access access, std::string&& prefix)
            : ISingleObjectProvider(access), m_prefix(std::move(prefix))
        {
        }

        Command command(Optional<const ToolCache&> tool_cache) const
        {
            if (tool_cache)
            {
                return Command{tool_cache.value_or_exit(VCPKG_LINE_INFO).get_tool_path(Tools::GSUTIL, stdout_sink)};
            }
            return Command{Tools::GSUTIL};
        }

        bool is_available(Optional<const ToolCache&> tool_cache, StringView object) const override
        {
            auto cmd = command(tool_cache)
                           .string_arg("-q")
                           .string_arg("stat")
                           .string_arg(Strings::concat(m_prefix, object.to_string()));
            return succeeded(cmd_execute(cmd));
        }
        void upload(Optional<const ToolCache&> tool_cache,
                    StringView object_id,
                    const Path& object,
                    MessageSink& msg_sink) override
        {
            auto cmd = command(tool_cache)
                           .string_arg("-q")
                           .string_arg("cp")
                           .string_arg(m_prefix + object_id.to_string())
                           .string_arg(object);
            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::GSUTIL);
            if (!out)
            {
                msg_sink.println(Color::warning, out.error());
            }
        }
        void download(Optional<const ToolCache&> tool_cache, StringView object, const Path& target_file) const override
        {
            auto cmd = command(tool_cache)
                           .string_arg("-q")
                           .string_arg("cp")
                           .string_arg(m_prefix + object.to_string())
                           .string_arg(target_file);
            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::GSUTIL);
            if (!out)
            {
                msg::write_unlocalized_text_to_stdout(Color::warning, out.error());
            }
        }

    private:
        std::string m_prefix;
    };

    struct AwsObjectProvider : ISingleObjectProvider
    {
        AwsObjectProvider(Access access, std::string&& prefix, const bool no_sign_request)
            : ISingleObjectProvider(access), m_prefix(std::move(prefix)), m_no_sign_request(no_sign_request)
        {
        }

        Command command(Optional<const ToolCache&> tool_cache) const
        {
            return tool_cache
                .map([](auto& tool_cache) { return Command{tool_cache.get_tool_path(Tools::AWSCLI, stdout_sink)}; })
                .value_or(Command{Tools::AWSCLI});
        }

        bool is_available(Optional<const ToolCache&> tool_cache, StringView object) const override
        {
            auto cmd =
                command(tool_cache).string_arg("s3").string_arg("ls").string_arg(Strings::concat(m_prefix, object));
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }

            return succeeded(cmd_execute(cmd));
        }

        void upload(Optional<const ToolCache&> tool_cache,
                    StringView object_id,
                    const Path& object,
                    MessageSink& msg_sink) override
        {
            auto cmd = command(tool_cache)
                           .string_arg("s3")
                           .string_arg("cp")
                           .string_arg(object)
                           .string_arg(Strings::concat(m_prefix, object_id));
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }
            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::AWSCLI);
            if (!out)
            {
                msg_sink.println(Color::warning, out.error());
            }
        }

        void download(Optional<const ToolCache&> tool_cache, StringView object, const Path& target_file) const override
        {
            auto cmd = command(tool_cache)
                           .string_arg("s3")
                           .string_arg("cp")
                           .string_arg(Strings::concat(m_prefix, object))
                           .string_arg(target_file);
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }

            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::AWSCLI);
            if (!out)
            {
                msg::write_unlocalized_text_to_stdout(Color::warning, out.error());
            }
        }

    private:
        std::string m_prefix;
        bool m_no_sign_request;
    };

    struct CosObjectProvider : ISingleObjectProvider
    {
        CosObjectProvider(Access access, std::string&& prefix)
            : ISingleObjectProvider(access), m_prefix(std::move(prefix))
        {
        }

        Command command(Optional<const ToolCache&> tool_cache) const
        {
            return tool_cache
                .map([](auto& tool_cache) { return Command{tool_cache.get_tool_path(Tools::COSCLI, stdout_sink)}; })
                .value_or(Command{"cos"});
        }

        bool is_available(Optional<const ToolCache&> tool_cache, StringView object) const override
        {
            auto cmd = command(tool_cache).string_arg("ls").string_arg(Strings::concat(m_prefix, object));
            return succeeded(cmd_execute(cmd));
        }

        void upload(Optional<const ToolCache&> tool_cache,
                    StringView object_id,
                    const Path& object,
                    MessageSink&) override
        {
            auto cmd = command(tool_cache)
                           .string_arg("cp")
                           .string_arg(object)
                           .string_arg(Strings::concat(m_prefix, object_id));
            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::COSCLI);
            if (!out)
            {
                msg::write_unlocalized_text_to_stdout(Color::warning, out.error());
            }
        }

        void download(Optional<const ToolCache&> tool_cache, StringView object, const Path& target_file) const override
        {
            auto cmd = command(tool_cache)
                           .string_arg("cp")
                           .string_arg(Strings::concat(m_prefix, object))
                           .string_arg(target_file);
            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::COSCLI);
            if (!out)
            {
                msg::write_unlocalized_text_to_stdout(Color::warning, out.error());
            }
        }

    private:
        std::string m_prefix;
    };
}

namespace vcpkg
{
    LocalizedString UrlTemplate::valid() const
    {
        static constexpr std::array<StringLiteral, 1> valid_keys = {"sha"};
        return valid(valid_keys);
    }

    std::string UrlTemplate::instantiate_variable(StringView sha) const
    {
        return api_stable_format(url_template,
                                 [&](std::string& out, StringView key) {
                                     if (key == "sha")
                                     {
                                         Strings::append(out, sha);
                                     }
                                     else
                                     {
                                         Debug::println("Unknown key: ", key);
                                         // We do a input validation while parsing the config
                                         Checks::unreachable(VCPKG_LINE_INFO);
                                     };
                                 })
            .value_or_exit(VCPKG_LINE_INFO);
    }

    LocalizedString UrlTemplate::valid(View<StringLiteral> valid_keys) const
    {
        std::vector<std::string> invalid_keys;
        auto result = api_stable_format(url_template, [&](std::string&, StringView key) {
            if (!Util::Vectors::contains(valid_keys, key))
            {
                invalid_keys.push_back(key.to_string());
            }
        });
        if (!result)
        {
            return result.error();
        }
        if (!invalid_keys.empty())
        {
            return msg::format(msgUnknownVariablesInTemplate,
                               msg::value = url_template,
                               msg::list = Strings::join(", ", invalid_keys));
        }
        return {};
    }

    LocalizedString ExtendedUrlTemplate::valid() const
    {
        static constexpr std::array<StringLiteral, 4> valid_keys = {"name", "version", "sha", "triplet"};
        return UrlTemplate::valid(valid_keys);
    }

    std::string ExtendedUrlTemplate::instantiate_variables(const BinaryPackageInformation& info) const
    {
        return api_stable_format(url_template,
                                 [&](std::string& out, StringView key) {
                                     if (key == "version")
                                     {
                                         out += info.raw_version;
                                     }
                                     else if (key == "name")
                                     {
                                         out += info.spec.name();
                                     }
                                     else if (key == "triplet")
                                     {
                                         out += info.spec.triplet().canonical_name();
                                     }
                                     else if (key == "sha")
                                     {
                                         out += info.package_abi;
                                     }
                                     else
                                     {
                                         Debug::println("Unknown key: ", key);
                                         // We do a input validation while parsing the config
                                         Checks::unreachable(VCPKG_LINE_INFO);
                                     };
                                 })
            .value_or_exit(VCPKG_LINE_INFO);
    }

    void BinaryCache::wait_for_async_complete()
    {
        bool have_remaining_packages = remaining_packages_to_push > 0;
        if (have_remaining_packages)
        {
            bg_msg_sink.print_published();
            msg::println(msgWaitUntilPackagesUploaded, msg::count = remaining_packages_to_push);
        }
        bg_msg_sink.publish_directly_to_out_sink();
        end_push_thread = true;
        actions_to_push_notifier.notify_all();
        push_thread.join();
    }

    BinaryCache::BinaryCache(Filesystem& filesystem)
        : bg_msg_sink(stdout_sink)
        , push_thread([this]() { push_thread_main(); })
        , end_push_thread{false}
        , filesystem(filesystem)
    {
    }

    BinaryCache::BinaryCache(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
        : BinaryCache(paths.get_filesystem())
    {
        install_providers_for(args, paths);
    }

    BinaryCache::~BinaryCache() { wait_for_async_complete(); }

    void BinaryCache::install_providers(std::vector<std::unique_ptr<IBinaryProvider>>&& providers)
    {
        Checks::check_exit(
            VCPKG_LINE_INFO, m_status.empty(), "Attempted to install additional providers in active binary cache");
        if (m_providers.empty())
        {
            m_providers = std::move(providers);
        }
        else
        {
            m_providers.insert(m_providers.end(),
                               std::make_move_iterator(providers.begin()),
                               std::make_move_iterator(providers.end()));
        }
        needs_nuspec_data = Util::any_of(m_providers, [](auto& provider) { return provider->needs_nuspec_data(); });
    }

    void BinaryCache::install_providers_for(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        if (args.binary_caching_enabled())
        {
            install_providers(
                create_binary_providers_from_configs(paths, args.binary_sources).value_or_exit(VCPKG_LINE_INFO));
        }
    }

    RestoreResult BinaryCache::try_restore(const InstallPlanAction& action)
    {
        const auto abi = action.package_abi().get();
        if (!abi)
        {
            // e.g. this is a `--head` package
            return RestoreResult::unavailable;
        }

        auto& cache_status = m_status[*abi];
        if (cache_status.is_restored())
        {
            return RestoreResult::restored;
        }

        const auto available = cache_status.get_available_provider();
        if (available)
        {
            switch (available->try_restore(action))
            {
                case RestoreResult::unavailable:
                    // Even though that provider thought it had it, it didn't; perhaps
                    // due to intermittent network problems etc.
                    // Try other providers below
                    break;
                case RestoreResult::restored: cache_status.mark_restored(); return RestoreResult::restored;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        for (auto&& provider : m_providers)
        {
            if (provider.get() == available)
            {
                continue; // this one already tried :)
            }

            if (cache_status.is_unavailable(m_providers.size()))
            {
                break;
            }

            switch (provider->try_restore(action))
            {
                case RestoreResult::restored: cache_status.mark_restored(); return RestoreResult::restored;
                case RestoreResult::unavailable: cache_status.mark_unavailable(provider.get()); break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        return RestoreResult::unavailable;
    }

    void BinaryCache::push_success(const InstallPlanAction& action, Path package_dir)
    {
        const auto abi = action.package_abi().get();
        if (abi)
        {
            const auto clean_packages = action.build_options.clean_packages == CleanPackages::YES;
            if (clean_packages)
            {
                static int counter = 0;
                Path new_packaged_dir = package_dir + "_push_" + std::to_string(++counter);
                filesystem.remove_all(new_packaged_dir, VCPKG_LINE_INFO);
                filesystem.rename(package_dir, new_packaged_dir, VCPKG_LINE_INFO);
                package_dir = new_packaged_dir;
            }

            std::string nuspec;
            if (needs_nuspec_data)
            {
                NugetReference nuget_ref = make_nugetref(action, get_nuget_prefix());
                nuspec = generate_nuspec(package_dir, action, nuget_ref);
            }
            std::unique_lock<std::mutex> lock(actions_to_push_mutex);
            remaining_packages_to_push++;
            actions_to_push.push_back(ActionToPush{
                BinaryProviderPushRequest{BinaryPackageInformation{action, std::move(nuspec)}, package_dir},
                clean_packages});
            actions_to_push_notifier.notify_all();
        }
    }

    void BinaryCache::print_push_success_messages() { bg_msg_sink.print_published(); }

    void BinaryCache::prefetch(View<InstallPlanAction> actions)
    {
        std::vector<CacheStatus*> cache_status{actions.size()};
        for (size_t idx = 0; idx < actions.size(); ++idx)
        {
            const auto abi = actions[idx].package_abi().get();
            if (abi)
            {
                cache_status[idx] = &m_status[*abi];
            }
        }

        for (auto&& provider : m_providers)
        {
            provider->prefetch(actions, cache_status);
            for (auto status : cache_status)
            {
                if (status)
                {
                    status->mark_unavailable(provider.get());
                }
            }
        }
    }

    std::vector<CacheAvailability> BinaryCache::precheck(View<InstallPlanAction> actions)
    {
        std::vector<CacheStatus*> cache_status{actions.size()};
        for (size_t idx = 0; idx < actions.size(); ++idx)
        {
            auto& action = actions[idx];
            const auto abi = action.package_abi().get();
            if (!abi)
            {
                Checks::unreachable(VCPKG_LINE_INFO, fmt::format("{} did not have an ABI", action.spec));
            }

            cache_status[idx] = &m_status[*abi];
        }

        for (auto&& provider : m_providers)
        {
            provider->precheck(actions, cache_status);
        }

        std::vector<CacheAvailability> results{actions.size()};
        for (size_t idx = 0; idx < results.size(); ++idx)
        {
            results[idx] = cache_status[idx]->get_available_provider() ? CacheAvailability::available
                                                                       : CacheAvailability::unavailable;
        }

        return results;
    }

    void BinaryCache::push_thread_main()
    {
        decltype(actions_to_push) my_tasks;
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(actions_to_push_mutex);
                actions_to_push_notifier.wait(lock, [this]() { return !actions_to_push.empty() || end_push_thread; });
                if (actions_to_push.empty())
                {
                    if (end_push_thread) break;
                    continue;
                }

                std::swap(my_tasks, actions_to_push);
            }
            // Now, consume all of `my_tasks` before taking the lock again.
            for (auto& action_to_push : my_tasks)
            {
                if (end_push_thread)
                {
                    msg::println(msgUploadRemainingPackages, msg::count = remaining_packages_to_push);
                }
                for (auto&& provider : m_providers)
                {
                    provider->push_success(action_to_push.request, bg_msg_sink);
                }
                if (action_to_push.clean_after_push)
                {
                    filesystem.remove_all(action_to_push.request.package_dir, VCPKG_LINE_INFO);
                }
                remaining_packages_to_push--;
            }
            my_tasks.clear();
        }
    }

    bool CacheStatus::should_attempt_precheck(const IBinaryProvider* sender) const noexcept
    {
        switch (m_status)
        {
            case CacheStatusState::unknown: return !Util::Vectors::contains(m_known_unavailable_providers, sender);
            case CacheStatusState::available: return false;
            case CacheStatusState::restored: return false;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    bool CacheStatus::should_attempt_restore(const IBinaryProvider* sender) const noexcept
    {
        switch (m_status)
        {
            case CacheStatusState::unknown: return !Util::Vectors::contains(m_known_unavailable_providers, sender);
            case CacheStatusState::available: return m_available_provider == sender;
            case CacheStatusState::restored: return false;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    bool CacheStatus::is_unavailable(size_t total_providers) const noexcept
    {
        switch (m_status)
        {
            case CacheStatusState::unknown: return m_known_unavailable_providers.size() <= total_providers;
            case CacheStatusState::available:
            case CacheStatusState::restored: return false;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    bool CacheStatus::is_restored() const noexcept { return m_status == CacheStatusState::restored; }

    void CacheStatus::mark_unavailable(const IBinaryProvider* sender)
    {
        switch (m_status)
        {
            case CacheStatusState::unknown:
                if (!Util::Vectors::contains(m_known_unavailable_providers, sender))
                {
                    m_known_unavailable_providers.push_back(sender);
                }
                break;
            case CacheStatusState::available:
            case CacheStatusState::restored: break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }
    void CacheStatus::mark_available(const IBinaryProvider* sender) noexcept
    {
        switch (m_status)
        {
            case CacheStatusState::unknown:
                m_known_unavailable_providers.~vector();
                m_status = CacheStatusState::available;
                m_available_provider = sender;
                break;
            case CacheStatusState::available:
            case CacheStatusState::restored: break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    void CacheStatus::mark_restored() noexcept
    {
        switch (m_status)
        {
            case CacheStatusState::unknown: m_known_unavailable_providers.~vector(); [[fallthrough]];
            case CacheStatusState::available: m_status = CacheStatusState::restored; break;
            case CacheStatusState::restored: break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    const IBinaryProvider* CacheStatus::get_available_provider() const noexcept
    {
        switch (m_status)
        {
            case CacheStatusState::available: return m_available_provider;
            case CacheStatusState::unknown:
            case CacheStatusState::restored: return nullptr;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    void BinaryConfigParserState::clear()
    {
        ObjectCacheConfig::clear();
        nuget_interactive = false;
        nugettimeout = "100";
        url_templates_to_get.clear();
        url_templates_to_put.clear();
        sources_to_read.clear();
        sources_to_write.clear();
        configs_to_read.clear();
        configs_to_write.clear();
        secrets.clear();
    }

    BinaryPackageInformation::BinaryPackageInformation(const InstallPlanAction& action, std::string&& nuspec)
        : package_abi(action.package_abi().value_or_exit(VCPKG_LINE_INFO))
        , spec(action.spec)
        , raw_version(action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO)
                          .source_control_file->core_paragraph->raw_version)
        , nuspec(std::move(nuspec))
    {
    }

    void ObjectCacheConfig::clear()
    {
        object_providers.clear();
        secrets.clear();
    }
}

namespace
{
    ExpectedL<Path> default_cache_path_impl()
    {
        auto maybe_cachepath = get_environment_variable("VCPKG_DEFAULT_BINARY_CACHE");
        if (auto p_str = maybe_cachepath.get())
        {
            get_global_metrics_collector().track_define(DefineMetric::VcpkgDefaultBinaryCache);
            Path path = std::move(*p_str);
            path.make_preferred();
            if (!get_real_filesystem().is_directory(path))
            {
                return msg::format(msgDefaultBinaryCacheRequiresDirectory, msg::path = path);
            }

            if (!path.is_absolute())
            {
                return msg::format(msgDefaultBinaryCacheRequiresAbsolutePath, msg::path = path);
            }

            return std::move(path);
        }

        return get_platform_cache_vcpkg().then([](Path p) -> ExpectedL<Path> {
            if (p.is_absolute())
            {
                p /= "archives";
                p.make_preferred();
                return std::move(p);
            }

            return msg::format(msgDefaultBinaryCachePlatformCacheRequiresAbsolutePath, msg::path = p);
        });
    }

    const ExpectedL<Path>& default_cache_path()
    {
        static auto cachepath = default_cache_path_impl();
        return cachepath;
    }

    struct ObjectCacheConfigParser : ConfigSegmentsParser
    {
        ObjectCacheConfigParser(Filesystem& fs, StringView text, StringView origin, ObjectCacheConfig* state)
            : ConfigSegmentsParser(text, origin), fs(fs), state(state)
        {
        }

        Filesystem& fs;
        ObjectCacheConfig* state;
        bool aws_no_sign_request = false;

        void parse()
        {
            auto all_segments = parse_all_segments();
            // new parser is stateless, but we want to keep x-aws-config,no-sign-request for backcompat
            for (const auto& segment : all_segments)
            {
                if (segment.size() == 2 && segment[0].second == "x-aws-config" &&
                    segment[1].second == "no-sign-request")
                {
                    aws_no_sign_request = true;
                }
            }
            for (auto&& x : all_segments)
            {
                if (get_error()) return;
                handle_segments(std::move(x));
            }
        }

        virtual void handle_segments(std::vector<std::pair<SourceLoc, std::string>>&& segments)
        {
            Checks::check_exit(VCPKG_LINE_INFO, !segments.empty());
            if (segments[0].second == "clear")
            {
                if (segments.size() >= 2)
                {
                    return add_error(msg::format(msgAssetCacheProviderAcceptsNoArguments, msg::value = "clear"),
                                     segments[1].first);
                }

                state->clear();
            }
            else if (segments[0].second == "files")
            {
                if (segments.size() < 2)
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresPathArgument, msg::binary_source = "files"),
                                     segments[0].first);
                }

                Path p = segments[1].second;
                if (!p.is_absolute())
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresAbsolutePath, msg::binary_source = "files"),
                                     segments[1].first);
                }

                auto maybe_access = parse_readwrite(segments, 2);
                if (segments.size() > 3)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresOneOrTwoArguments, msg::binary_source = "files"),
                        segments[3].first);
                }
                if (maybe_access)
                {
                    state->object_providers.push_back(std::make_unique<FileObjectProvider>(
                        maybe_access.value_or_exit(VCPKG_LINE_INFO), fs, std::move(p)));
                }
            }
            else if (segments[0].second == "x-azblob")
            {
                // Scheme: x-azblob,<baseurl>,<sas>[,<readwrite>]
                if (segments.size() < 3)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresBaseUrlAndToken, msg::binary_source = "azblob"),
                        segments[0].first);
                }

                if (!Strings::starts_with(segments[1].second, "https://"))
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresBaseUrl,
                                                 msg::base_url = "https://",
                                                 msg::binary_source = "azblob"),
                                     segments[1].first);
                }

                if (Strings::starts_with(segments[2].second, "?"))
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresValidToken, msg::binary_source = "azblob"),
                                     segments[2].first);
                }

                if (segments.size() > 4)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresTwoOrThreeArguments, msg::binary_source = "azblob"),
                        segments[4].first);
                }

                auto p = segments[1].second;
                if (p.back() != '/')
                {
                    p.push_back('/');
                }

                p.append("{sha}.zip");
                if (!Strings::starts_with(segments[2].second, "?"))
                {
                    p.push_back('?');
                }

                p.append(segments[2].second);
                state->secrets.push_back(segments[2].second);
                UrlTemplate url_template = {p};
                auto headers = azure_blob_headers();
                url_template.headers_for_put.assign(headers.begin(), headers.end());
                auto maybe_access = parse_readwrite(segments, 3);
                if (maybe_access)
                {
                    state->object_providers.push_back(std::make_unique<HttpObjectProvider>(
                        maybe_access.value_or_exit(VCPKG_LINE_INFO), fs, std::move(url_template), state->secrets));
                }
            }
            else if (segments[0].second == "x-gcs")
            {
                // Scheme: x-gcs,<prefix>[,<readwrite>]
                if (segments.size() < 2)
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresPrefix, msg::binary_source = "gcs"),
                                     segments[0].first);
                }

                if (!Strings::starts_with(segments[1].second, "gs://"))
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresBaseUrl,
                                                 msg::base_url = "gs://",
                                                 msg::binary_source = "gcs"),
                                     segments[1].first);
                }

                if (segments.size() > 3)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresOneOrTwoArguments, msg::binary_source = "gcs"),
                        segments[3].first);
                }

                auto p = segments[1].second;
                if (p.back() != '/')
                {
                    p.push_back('/');
                }

                auto maybe_access = parse_readwrite(segments, 2);
                if (maybe_access)
                {
                    state->object_providers.push_back(
                        std::make_unique<GcsObjectProvider>(maybe_access.value_or_exit(VCPKG_LINE_INFO), std::move(p)));
                }
            }
            else if (segments[0].second == "x-aws")
            {
                // Scheme: x-aws,<prefix>[,<readwrite>]
                if (segments.size() < 2)
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresPrefix, msg::binary_source = "aws"),
                                     segments[0].first);
                }

                if (!Strings::starts_with(segments[1].second, "s3://"))
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresBaseUrl,
                                                 msg::base_url = "s3://",
                                                 msg::binary_source = "aws"),
                                     segments[1].first);
                }

                if (segments.size() > 3)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresOneOrTwoArguments, msg::binary_source = "aws"),
                        segments[3].first);
                }

                auto p = segments[1].second;
                if (p.back() != '/')
                {
                    p.push_back('/');
                }

                auto maybe_access = parse_readwrite(segments, 2);
                if (maybe_access)
                {
                    state->object_providers.push_back(std::make_unique<AwsObjectProvider>(
                        maybe_access.value_or_exit(VCPKG_LINE_INFO), std::move(p), aws_no_sign_request));
                }
            }
            else if (segments[0].second == "x-aws-config")
            {
                if (segments.size() != 2)
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresSingleStringArgument,
                                                 msg::binary_source = "x-aws-config"));
                }

                if (segments[1].second != "no-sign-request")
                {
                    return add_error(msg::format(msgInvalidArgument), segments[1].first);
                }
                // already handled in parse(), but do input validation here
            }
            else if (segments[0].second == "x-cos")
            {
                // Scheme: x-cos,<prefix>[,<readwrite>]
                if (segments.size() < 2)
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresPrefix, msg::binary_source = "cos"),
                                     segments[0].first);
                }

                if (!Strings::starts_with(segments[1].second, "cos://"))
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresBaseUrl,
                                                 msg::base_url = "cos://",
                                                 msg::binary_source = "cos"),
                                     segments[1].first);
                }

                if (segments.size() > 3)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresOneOrTwoArguments, msg::binary_source = "cos"),
                        segments[3].first);
                }

                auto p = segments[1].second;
                if (p.back() != '/')
                {
                    p.push_back('/');
                }

                auto maybe_access = parse_readwrite(segments, 2);
                if (maybe_access)
                {
                    state->object_providers.push_back(
                        std::make_unique<CosObjectProvider>(maybe_access.value_or_exit(VCPKG_LINE_INFO), std::move(p)));
                }
            }
            else if (segments[0].second == "http")
            {
                // Scheme: http,<url_template>[,<readwrite>[,<header>]]
                if (segments.size() < 2)
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresPrefix, msg::binary_source = "http"),
                                     segments[0].first);
                }

                if (!Strings::starts_with(segments[1].second, "http://") &&
                    !Strings::starts_with(segments[1].second, "https://"))
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresBaseUrl,
                                                 msg::base_url = "https://",
                                                 msg::binary_source = "http"),
                                     segments[1].first);
                }

                if (segments.size() > 4)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresTwoOrThreeArguments, msg::binary_source = "http"),
                        segments[3].first);
                }

                UrlTemplate url_template{segments[1].second};
                if (auto err = url_template.valid(); !err.empty())
                {
                    return add_error(std::move(err), segments[1].first);
                }
                if (segments.size() == 4)
                {
                    url_template.headers_for_get.push_back(segments[3].second);
                    url_template.headers_for_put.push_back(segments[3].second);
                }

                auto maybe_access = parse_readwrite(segments, 2);
                if (maybe_access)
                {
                    state->object_providers.push_back(std::make_unique<HttpObjectProvider>(
                        maybe_access.value_or_exit(VCPKG_LINE_INFO), fs, std::move(url_template), state->secrets));
                }
            }
            else
            {
                return add_error(msg::format(msgUnknownBinaryProviderType), segments[0].first);
            }
        }
    };

    struct BinaryConfigParser : ObjectCacheConfigParser
    {
        BinaryConfigParser(Filesystem& fs, StringView text, StringView origin, BinaryConfigParserState* state)
            : ObjectCacheConfigParser(fs, text, origin, state), state(state)
        {
        }

        BinaryConfigParserState* state;

        void handle_segments(std::vector<std::pair<SourceLoc, std::string>>&& segments)
        {
            Checks::check_exit(VCPKG_LINE_INFO, !segments.empty());
            std::string source = segments[0].second;
            if (segments[0].second == "interactive")
            {
                if (segments.size() > 1)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresNoneArguments, msg::binary_source = "interactive"),
                        segments[1].first);
                }

                state->nuget_interactive = true;
            }
            else if (segments[0].second == "nugetconfig")
            {
                if (segments.size() < 2)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresSourceArgument, msg::binary_source = "nugetconfig"),
                        segments[0].first);
                }

                Path p = segments[1].second;
                if (!p.is_absolute())
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresAbsolutePath, msg::binary_source = "nugetconfig"),
                        segments[1].first);
                }

                handle_readwrite(state->configs_to_read, state->configs_to_write, std::move(p), segments, 2);
                if (segments.size() > 3)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresOneOrTwoArguments, msg::binary_source = "nugetconfig"),
                        segments[3].first);
                }
            }
            else if (segments[0].second == "nuget")
            {
                if (segments.size() < 2)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresSourceArgument, msg::binary_source = "nuget"),
                        segments[0].first);
                }

                auto&& p = segments[1].second;
                if (p.empty())
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresSourceArgument, msg::binary_source = "nuget"));
                }

                handle_readwrite(state->sources_to_read, state->sources_to_write, std::move(p), segments, 2);
                if (segments.size() > 3)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresOneOrTwoArguments, msg::binary_source = "nuget"),
                        segments[3].first);
                }
            }
            else if (segments[0].second == "nugettimeout")
            {
                if (segments.size() != 2)
                {
                    return add_error(msg::format(msgNugetTimeoutExpectsSinglePositiveInteger));
                }

                auto&& t = segments[1].second;
                if (t.empty())
                {
                    return add_error(msg::format(msgNugetTimeoutExpectsSinglePositiveInteger));
                }
                char* end;
                long timeout = std::strtol(t.c_str(), &end, 0);
                if (*end != '\0' || timeout <= 0)
                {
                    return add_error(msg::format(msgNugetTimeoutExpectsSinglePositiveInteger));
                }

                state->nugettimeout = std::to_string(timeout);
            }
            else if (segments[0].second == "default")
            {
                if (segments.size() > 2)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresSingleArgument, msg::binary_source = "default"),
                        segments[0].first);
                }

                const auto& maybe_home = default_cache_path();
                if (!maybe_home)
                {
                    return add_error(LocalizedString{maybe_home.error()}, segments[0].first);
                }

                auto maybe_access = parse_readwrite(segments, 1);
                if (maybe_access)
                {
                    state->object_providers.push_back(std::make_unique<FileObjectProvider>(
                        maybe_access.value_or_exit(VCPKG_LINE_INFO), fs, Path(*maybe_home.get())));
                }
            }
            else if (segments[0].second == "x-gha")
            {
                // Scheme: x-gha[,<readwrite>]
                if (segments.size() > 2)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresZeroOrOneArgument, msg::binary_source = "gha"),
                        segments[2].first);
                }

                state->gha_access = parse_readwrite(segments, 1);
            }
            else if (segments[0].second == "http")
            {
                // Scheme: http,<url_template>[,<readwrite>[,<header>]]
                if (segments.size() < 2)
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresPrefix, msg::binary_source = "http"),
                                     segments[0].first);
                }

                if (!Strings::starts_with(segments[1].second, "http://") &&
                    !Strings::starts_with(segments[1].second, "https://"))
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresBaseUrl,
                                                 msg::base_url = "https://",
                                                 msg::binary_source = "http"),
                                     segments[1].first);
                }

                if (segments.size() > 4)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresTwoOrThreeArguments, msg::binary_source = "http"),
                        segments[3].first);
                }

                ExtendedUrlTemplate url_template{UrlTemplate{segments[1].second}};
                if (auto err = url_template.valid(); !err.empty())
                {
                    return add_error(std::move(err), segments[1].first);
                }
                if (segments.size() == 4)
                {
                    url_template.headers_for_get.push_back(segments[3].second);
                    url_template.headers_for_put.push_back(segments[3].second);
                }

                handle_readwrite(
                    state->url_templates_to_get, state->url_templates_to_put, std::move(url_template), segments, 2);
            }
            else
            {
                ObjectCacheConfigParser::handle_segments(std::move(segments));
            }

            static const std::map<StringView, DefineMetric> metric_names{
                {"x-aws", DefineMetric::BinaryCachingAws},
                {"x-azblob", DefineMetric::BinaryCachingAzBlob},
                {"x-cos", DefineMetric::BinaryCachingCos},
                {"default", DefineMetric::BinaryCachingDefault},
                {"files", DefineMetric::BinaryCachingFiles},
                {"x-gcs", DefineMetric::BinaryCachingGcs},
                {"http", DefineMetric::BinaryCachingHttp},
                {"nuget", DefineMetric::BinaryCachingNuget},
            };

            MetricsSubmission metrics;

            auto it = metric_names.find(StringView(source.c_str()));
            if (it != metric_names.end())
            {
                metrics.track_define(it->second);
            }

            get_global_metrics_collector().track_submission(std::move(metrics));
        }
    };

    struct AssetSourcesParser : ObjectCacheConfigParser
    {
        AssetSourcesParser(Filesystem& fs, StringView text, StringView origin, DownloadManagerConfig* state)
            : ObjectCacheConfigParser(fs, text, origin, state), state(state)
        {
        }

        DownloadManagerConfig* state;

        void handle_segments(std::vector<std::pair<SourceLoc, std::string>>&& segments)
        {
            Checks::check_exit(VCPKG_LINE_INFO, !segments.empty());

            if (segments[0].second == "x-block-origin")
            {
                if (segments.size() >= 2)
                {
                    return add_error(
                        msg::format(msgAssetCacheProviderAcceptsNoArguments, msg::value = "x-block-origin"),
                        segments[1].first);
                }

                state->block_origin = true;
            }
            else if (segments[0].second == "x-azurl")
            {
                // Scheme: x-azurl,<baseurl>[,<sas>[,<readwrite>]]
                if (segments.size() < 2)
                {
                    return add_error(msg::format(msgAzUrlAssetCacheRequiresBaseUrl), segments[0].first);
                }

                if (segments.size() > 4)
                {
                    return add_error(msg::format(msgAzUrlAssetCacheRequiresLessThanFour), segments[4].first);
                }

                if (segments[1].second.empty())
                {
                    return add_error(msg::format(msgAzUrlAssetCacheRequiresBaseUrl), segments[1].first);
                }

                auto p = segments[1].second;
                if (p.back() != '/')
                {
                    p.push_back('/');
                }

                p.append("<SHA>");
                if (segments.size() > 2 && !segments[2].second.empty())
                {
                    if (!Strings::starts_with(segments[2].second, "?"))
                    {
                        p.push_back('?');
                    }
                    p.append(segments[2].second);
                    // Note: the download manager does not currently respect secrets
                    state->secrets.push_back(segments[2].second);
                }
                UrlTemplate url_template = {p};
                auto headers = azure_blob_headers();
                url_template.headers_for_put.assign(headers.begin(), headers.end());
                auto maybe_access = parse_readwrite(segments, 3);
                if (maybe_access)
                {
                    state->object_providers.push_back(std::make_unique<HttpObjectProvider>(
                        maybe_access.value_or_exit(VCPKG_LINE_INFO), fs, std::move(url_template), state->secrets));
                }
            }
            else if (segments[0].second == "x-script")
            {
                // Scheme: x-script,<script-template>
                if (segments.size() != 2)
                {
                    return add_error(msg::format(msgScriptAssetCacheRequiresScript), segments[0].first);
                }
                state->script = segments[1].second;
            }
            else
            {
                ObjectCacheConfigParser::handle_segments(std::move(segments));
                // Don't forget to update this message if new providers are added.
                return add_error(msg::format(msgUnexpectedAssetCacheProvider), segments[0].first);
            }
        }
    };
}

void DownloadManagerConfig::clear()
{
    ObjectCacheConfig::clear();
    block_origin = false;
    script = nullopt;
}
ExpectedL<DownloadManagerConfig> vcpkg::parse_download_configuration(Filesystem& fs, const Optional<std::string>& arg)
{
    if (!arg || arg.get()->empty()) return DownloadManagerConfig{};

    get_global_metrics_collector().track_define(DefineMetric::AssetSource);

    DownloadManagerConfig s;
    const auto source = Strings::concat("$", VcpkgCmdArguments::ASSET_SOURCES_ENV);
    AssetSourcesParser parser(fs, *arg.get(), source, &s);
    parser.parse();
    if (auto err = parser.get_error())
    {
        return LocalizedString::from_raw(err->to_string()) // note that this already contains error:
            .append_raw('\n')
            .append(msg::msgNoteMessage)
            .append(msg::msgSeeURL, msg::url = docs::assetcaching_url);
    }
    return s;
}

ExpectedL<BinaryConfigParserState> vcpkg::create_binary_providers_from_configs_pure(Filesystem& fs,
                                                                                    const std::string& env_string,
                                                                                    View<std::string> args)
{
    if (!env_string.empty())
    {
        get_global_metrics_collector().track_define(DefineMetric::VcpkgBinarySources);
    }

    if (args.size() != 0)
    {
        get_global_metrics_collector().track_define(DefineMetric::BinaryCachingSource);
    }

    BinaryConfigParserState s;

    BinaryConfigParser default_parser(fs, "default,readwrite", "<defaults>", &s);
    default_parser.parse();
    if (auto err = default_parser.get_error())
    {
        return LocalizedString::from_raw(err->message);
    }

    BinaryConfigParser env_parser(fs, env_string, "VCPKG_BINARY_SOURCES", &s);
    env_parser.parse();
    if (auto err = env_parser.get_error())
    {
        return LocalizedString::from_raw(err->to_string());
    }

    for (auto&& arg : args)
    {
        BinaryConfigParser arg_parser(fs, arg, "<command>", &s);
        arg_parser.parse();
        if (auto err = arg_parser.get_error())
        {
            return LocalizedString::from_raw(err->to_string());
        }
    }

    return s;
}

ExpectedL<std::vector<std::unique_ptr<IBinaryProvider>>> vcpkg::create_binary_providers_from_configs(
    const VcpkgPaths& paths, View<std::string> args)
{
    std::string env_string = get_environment_variable("VCPKG_BINARY_SOURCES").value_or("");
    if (Debug::g_debugging)
    {
        const auto& maybe_cachepath = default_cache_path();
        if (const auto cachepath = maybe_cachepath.get())
        {
            Debug::print("Default binary cache path is: ", *cachepath, '\n');
        }
        else
        {
            Debug::print("No binary cache path. Reason: ", maybe_cachepath.error(), '\n');
        }
    }

    Filesystem& fs = paths.get_filesystem();
    auto sRawHolder = create_binary_providers_from_configs_pure(fs, env_string, args);
    if (!sRawHolder)
    {
        return sRawHolder.error();
    }

    auto& s = sRawHolder.value_or_exit(VCPKG_LINE_INFO);
    std::vector<std::unique_ptr<IBinaryProvider>> providers;

    if (s.gha_access.has_value())
    {
        auto url = get_environment_variable("ACTIONS_CACHE_URL");
        auto token = get_environment_variable("ACTIONS_RUNTIME_TOKEN");
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               (url.has_value() && token.has_value()),
                               msgGHAParametersMissing,
                               msg::url = "https://learn.microsoft.com/en-us/vcpkg/users/binarycaching#gha");
        s.object_providers.push_back(std::make_unique<GHABinaryProvider>(s.gha_access.value_or_exit(VCPKG_LINE_INFO),
                                                                         fs,
                                                                         url.value_or_exit(VCPKG_LINE_INFO),
                                                                         token.value_or_exit(VCPKG_LINE_INFO)));
    }

    if (s.object_providers.size() > 0 || s.url_templates_to_put.size() > 0)
    {
        providers.push_back(std::make_unique<BinaryObjectProvider>(
            paths, std::move(s.object_providers), std::move(s.url_templates_to_put), s.secrets));
    }

    if (!s.sources_to_read.empty() || !s.sources_to_write.empty() || !s.configs_to_read.empty() ||
        !s.configs_to_write.empty())
    {
        providers.push_back(std::make_unique<NugetBinaryProvider>(paths,
                                                                  std::move(s.sources_to_read),
                                                                  std::move(s.sources_to_write),
                                                                  std::move(s.configs_to_read),
                                                                  std::move(s.configs_to_write),
                                                                  std::move(s.nugettimeout),
                                                                  s.nuget_interactive));
    }

    return providers;
}

std::string vcpkg::format_version_for_nugetref(StringView version, StringView abi_tag)
{
    // this cannot use DotVersion::try_parse or DateVersion::try_parse,
    // since this is a subtly different algorithm
    // and ignores random extra stuff from the end

    ParsedExternalVersion parsed_version;
    if (try_extract_external_date_version(parsed_version, version))
    {
        parsed_version.normalize();
        return fmt::format(
            "{}.{}.{}-vcpkg{}", parsed_version.major, parsed_version.minor, parsed_version.patch, abi_tag);
    }

    if (!version.empty() && version[0] == 'v')
    {
        version = version.substr(1);
    }
    if (try_extract_external_dot_version(parsed_version, version))
    {
        parsed_version.normalize();
        return fmt::format(
            "{}.{}.{}-vcpkg{}", parsed_version.major, parsed_version.minor, parsed_version.patch, abi_tag);
    }

    return Strings::concat("0.0.0-vcpkg", abi_tag);
}

details::NuGetRepoInfo details::get_nuget_repo_info_from_env()
{
    auto vcpkg_nuget_repository = get_environment_variable("VCPKG_NUGET_REPOSITORY");
    if (auto p = vcpkg_nuget_repository.get())
    {
        get_global_metrics_collector().track_define(DefineMetric::VcpkgNugetRepository);
        return {std::move(*p)};
    }

    auto gh_repo = get_environment_variable("GITHUB_REPOSITORY").value_or("");
    if (gh_repo.empty())
    {
        return {};
    }

    auto gh_server = get_environment_variable("GITHUB_SERVER_URL").value_or("");
    if (gh_server.empty())
    {
        return {};
    }

    get_global_metrics_collector().track_define(DefineMetric::GitHubRepository);
    return {Strings::concat(gh_server, '/', gh_repo, ".git"),
            get_environment_variable("GITHUB_REF").value_or(""),
            get_environment_variable("GITHUB_SHA").value_or("")};
}

std::string vcpkg::generate_nuspec(const Path& package_dir,
                                   const InstallPlanAction& action,
                                   const vcpkg::NugetReference& ref,
                                   details::NuGetRepoInfo rinfo)
{
    auto& spec = action.spec;
    auto& scf = *action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).source_control_file;
    auto& version = scf.core_paragraph->raw_version;
    const auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);
    const auto& compiler_info = abi_info.compiler_info.value_or_exit(VCPKG_LINE_INFO);
    std::string description =
        Strings::concat("NOT FOR DIRECT USE. Automatically generated cache package.\n\n",
                        Strings::join("\n    ", scf.core_paragraph->description),
                        "\n\nVersion: ",
                        version,
                        "\nTriplet: ",
                        spec.triplet().to_string(),
                        "\nCXX Compiler id: ",
                        compiler_info.id,
                        "\nCXX Compiler version: ",
                        compiler_info.version,
                        "\nTriplet/Compiler hash: ",
                        abi_info.triplet_abi.value_or_exit(VCPKG_LINE_INFO),
                        "\nFeatures:",
                        Strings::join(",", action.feature_list, [](const std::string& s) { return " " + s; }),
                        "\nDependencies:\n");

    for (auto&& dep : action.package_dependencies)
    {
        Strings::append(description, "    ", dep.name(), '\n');
    }

    XmlSerializer xml;
    xml.open_tag("package").line_break();
    xml.open_tag("metadata").line_break();
    xml.simple_tag("id", ref.id).line_break();
    xml.simple_tag("version", ref.version).line_break();
    if (!scf.core_paragraph->homepage.empty())
    {
        xml.simple_tag("projectUrl", scf.core_paragraph->homepage);
    }

    xml.simple_tag("authors", "vcpkg").line_break();
    xml.simple_tag("description", description).line_break();
    xml.open_tag("packageTypes");
    xml.start_complex_open_tag("packageType").text_attr("name", "vcpkg").finish_self_closing_complex_tag();
    xml.close_tag("packageTypes").line_break();
    if (!rinfo.repo.empty())
    {
        xml.start_complex_open_tag("repository").text_attr("type", "git").text_attr("url", rinfo.repo);
        if (!rinfo.branch.empty())
        {
            xml.text_attr("branch", rinfo.branch);
        }

        if (!rinfo.commit.empty())
        {
            xml.text_attr("commit", rinfo.commit);
        }

        xml.finish_self_closing_complex_tag().line_break();
    }

    xml.close_tag("metadata").line_break();
    xml.open_tag("files");
    xml.start_complex_open_tag("file")
        .text_attr("src", package_dir / "**")
        .text_attr("target", "")
        .finish_self_closing_complex_tag();
    xml.close_tag("files").line_break();
    xml.close_tag("package").line_break();
    return std::move(xml.buf);
}

LocalizedString vcpkg::format_help_topic_asset_caching()
{
    HelpTableFormatter table;
    table.format("clear", msg::format(msgHelpCachingClear));
    table.format("x-azurl,<url>[,<sas>[,<rw>]]", msg::format(msgHelpAssetCachingAzUrl));
    table.format("x-script,<template>", msg::format(msgHelpAssetCachingScript));
    table.format("x-block-origin", msg::format(msgHelpAssetCachingBlockOrigin));
    return msg::format(msgHelpAssetCaching)
        .append_raw('\n')
        .append_raw(table.m_str)
        .append_raw('\n')
        .append(msgExtendedDocumentationAtUrl, msg::url = docs::assetcaching_url);
}

LocalizedString vcpkg::format_help_topic_binary_caching()
{
    HelpTableFormatter table;

    // General sources:
    table.format("clear", msg::format(msgHelpCachingClear));
    const auto& maybe_cachepath = default_cache_path();
    if (auto p = maybe_cachepath.get())
    {
        table.format("default[,<rw>]", msg::format(msgHelpBinaryCachingDefaults, msg::path = *p));
    }
    else
    {
        table.format("default[,<rw>]", msg::format(msgHelpBinaryCachingDefaultsError));
    }

    table.format("files,<path>[,<rw>]", msg::format(msgHelpBinaryCachingFiles));
    table.format("http,<url_template>[,<rw>[,<header>]]", msg::format(msgHelpBinaryCachingHttp));
    table.format("x-azblob,<url>,<sas>[,<rw>]", msg::format(msgHelpBinaryCachingAzBlob));
    table.format("x-gcs,<prefix>[,<rw>]", msg::format(msgHelpBinaryCachingGcs));
    table.format("x-cos,<prefix>[,<rw>]", msg::format(msgHelpBinaryCachingCos));
    table.blank();

    // NuGet sources:
    table.header(msg::format(msgHelpBinaryCachingNuGetHeader));
    table.format("nuget,<uri>[,<rw>]", msg::format(msgHelpBinaryCachingNuGet));
    table.format("nugetconfig,<path>[,<rw>]", msg::format(msgHelpBinaryCachingNuGetConfig));
    table.format("nugettimeout,<seconds>", msg::format(msgHelpBinaryCachingNuGetTimeout));
    table.format("interactive", msg::format(msgHelpBinaryCachingNuGetInteractive));
    table.text(msg::format(msgHelpBinaryCachingNuGetFooter), 2);
    table.text("\n<repository type=\"git\" url=\"${VCPKG_NUGET_REPOSITORY}\"/>\n"
               "<repository type=\"git\"\n"
               "            url=\"${GITHUB_SERVER_URL}/${GITHUB_REPOSITORY}.git\"\n"
               "            branch=\"${GITHUB_REF}\"\n"
               "            commit=\"${GITHUB_SHA}\"/>",
               4);
    table.blank();

    // AWS sources:
    table.blank();
    table.header(msg::format(msgHelpBinaryCachingAwsHeader));
    table.format("x-aws,<prefix>[,<rw>]", msg::format(msgHelpBinaryCachingAws));
    table.format("x-aws-config,<parameter>", msg::format(msgHelpBinaryCachingAwsConfig));

    return msg::format(msgHelpBinaryCaching)
        .append_raw('\n')
        .append_raw(table.m_str)
        .append_raw('\n')
        .append(msgExtendedDocumentationAtUrl, msg::url = docs::binarycaching_url);
}

std::string vcpkg::generate_nuget_packages_config(const ActionPlan& action)
{
    auto refs = Util::fmap(action.install_actions,
                           [&](const InstallPlanAction& ipa) { return make_nugetref(ipa, get_nuget_prefix()); });
    XmlSerializer xml;
    xml.emit_declaration().line_break();
    xml.open_tag("packages").line_break();
    for (auto&& ref : refs)
    {
        xml.start_complex_open_tag("package")
            .text_attr("id", ref.id)
            .text_attr("version", ref.version)
            .finish_self_closing_complex_tag()
            .line_break();
    }

    xml.close_tag("packages").line_break();
    return std::move(xml.buf);
}
