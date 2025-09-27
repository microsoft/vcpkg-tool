#include <vcpkg/base/api-stable-format.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/message_sinks.h>
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
#include <vcpkg/dependencies.h>
#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <memory>
#include <utility>

using namespace vcpkg;

namespace
{
    // The length of an ABI in the binary cache
    static constexpr size_t ABI_LENGTH = 64;

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

        void handle_readwrite(bool& read,
                              bool& write,
                              const std::vector<std::pair<SourceLoc, std::string>>& segments,
                              size_t segment_idx)
        {
            if (segment_idx >= segments.size())
            {
                read = true;
                return;
            }

            auto& mode = segments[segment_idx].second;

            if (mode == "read")
            {
                read = true;
            }
            else if (mode == "write")
            {
                write = true;
            }
            else if (mode == "readwrite")
            {
                read = true;
                write = true;
            }
            else
            {
                return add_error(msg::format(msgExpectedReadWriteReadWrite), segments[segment_idx].first);
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

            if (messages().any_errors())
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

    FeedReference make_feedref(const PackageSpec& spec, const Version& version, StringView abi_tag, StringView prefix)
    {
        return {Strings::concat(prefix, spec.dir()), format_version_for_feedref(version.text, abi_tag)};
    }
    FeedReference make_feedref(const BinaryPackageReadInfo& info, StringView prefix)
    {
        return make_feedref(info.spec, info.version, info.package_abi, prefix);
    }

    void clean_prepare_dir(const Filesystem& fs, const Path& dir)
    {
        fs.remove_all(dir, VCPKG_LINE_INFO);
        if (!fs.create_directories(dir, VCPKG_LINE_INFO))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgUnableToClearPath, msg::path = dir);
        }
    }

    Path make_temp_archive_path(const Path& buildtrees, const PackageSpec& spec, const std::string& abi)
    {
        return buildtrees / fmt::format("{}_{}.zip", spec.name(), abi);
    }

    Path files_archive_parent_path(const std::string& abi) { return Path(abi.substr(0, 2)); }
    Path files_archive_subpath(const std::string& abi) { return files_archive_parent_path(abi) / (abi + ".zip"); }

    struct FilesWriteBinaryProvider : IWriteBinaryProvider
    {
        FilesWriteBinaryProvider(const Filesystem& fs, std::vector<Path>&& dirs) : m_fs(fs), m_dirs(std::move(dirs)) { }

        size_t push_success(const BinaryPackageWriteInfo& request, MessageSink& msg_sink) override
        {
            const auto& zip_path = request.zip_path.value_or_exit(VCPKG_LINE_INFO);
            size_t count_stored = 0;
            // Can't rename if zip_path should be coppied to multiple locations;
            // otherwise, the original file would be gone.
            const bool can_attempt_rename = m_dirs.size() == 1 && request.unique_write_provider;
            for (const auto& archives_root_dir : m_dirs)
            {
                const auto archive_parent_path = archives_root_dir / files_archive_parent_path(request.package_abi);
                m_fs.create_directories(archive_parent_path, IgnoreErrors{});
                const auto archive_path = archive_parent_path / (request.package_abi + ".zip");
                const auto archive_temp_path = Path(fmt::format("{}.{}", archive_path.native(), get_process_id()));
                std::error_code ec;
                if (can_attempt_rename)
                {
                    m_fs.rename_or_delete(zip_path, archive_path, ec);
                }

                if (!can_attempt_rename || (ec && ec == std::make_error_condition(std::errc::cross_device_link)))
                {
                    // either we need to make a copy or the rename failed because buildtrees and the binary
                    // cache write target are on different filesystems, copy to a sibling in that directory and rename
                    // into place
                    // First copy to temporary location to avoid race between different vcpkg instances trying to upload
                    // the same archive, e.g. if 2 machines try to upload to a shared binary cache.
                    m_fs.copy_file(zip_path, archive_temp_path, CopyOptions::overwrite_existing, ec);
                    if (!ec)
                    {
                        m_fs.rename_or_delete(archive_temp_path, archive_path, ec);
                    }
                }

                if (ec)
                {
                    msg_sink.println(Color::warning,
                                     msg::format(msgFailedToStoreBinaryCache, msg::path = archive_path)
                                         .append_raw('\n')
                                         .append_raw(ec.message()));
                }
                else
                {
                    count_stored++;
                }
            }
            return count_stored;
        }

        bool needs_nuspec_data() const override { return false; }
        bool needs_zip_file() const override { return true; }

    private:
        const Filesystem& m_fs;
        std::vector<Path> m_dirs;
    };

    enum class RemoveWhen
    {
        nothing,
        always,
    };

    struct ZipResource
    {
        ZipResource(Path&& p, RemoveWhen t) : path(std::move(p)), to_remove(t) { }

        Path path;
        RemoveWhen to_remove;
    };

    // This middleware class contains logic for BinaryProviders that operate on zip files.
    // Derived classes must implement:
    // - acquire_zips()
    // - IReadBinaryProvider::precheck()
    struct ZipReadBinaryProvider : IReadBinaryProvider
    {
        ZipReadBinaryProvider(ZipTool zip, const Filesystem& fs) : m_zip(std::move(zip)), m_fs(fs) { }

        void fetch(View<const InstallPlanAction*> actions, Span<RestoreResult> out_status) const override
        {
            const ElapsedTimer timer;
            std::vector<Optional<ZipResource>> zip_paths(actions.size(), nullopt);
            acquire_zips(actions, zip_paths);

            std::vector<std::pair<Command, uint64_t>> jobs_with_size;
            std::vector<size_t> action_idxs;
            for (size_t i = 0; i < actions.size(); ++i)
            {
                if (!zip_paths[i]) continue;
                const auto& pkg_path = actions[i]->package_dir.value_or_exit(VCPKG_LINE_INFO);
                clean_prepare_dir(m_fs, pkg_path);
                jobs_with_size.emplace_back(m_zip.decompress_zip_archive_cmd(pkg_path, zip_paths[i].get()->path),
                                            m_fs.file_size(zip_paths[i].get()->path, VCPKG_LINE_INFO));
                action_idxs.push_back(i);
            }
            std::sort(jobs_with_size.begin(), jobs_with_size.end(), [](const auto& l, const auto& r) {
                return l.second > r.second;
            });

            std::vector<Command> sorted_jobs;
            for (auto&& e : jobs_with_size)
            {
                sorted_jobs.push_back(std::move(e.first));
            }
            auto job_results = decompress_in_parallel(sorted_jobs);

            for (size_t j = 0; j < sorted_jobs.size(); ++j)
            {
                const auto i = action_idxs[j];
                const auto& zip_path = zip_paths[i].value_or_exit(VCPKG_LINE_INFO);
                if (job_results[j])
                {
#ifdef _WIN32
                    // On windows the ziptool does restore file times, we don't want that because this breaks file time
                    // based change detection.
                    const auto& pkg_path = actions[i]->package_dir.value_or_exit(VCPKG_LINE_INFO);
                    auto now = m_fs.file_time_now();
                    for (auto&& path : m_fs.get_files_recursive(pkg_path, VCPKG_LINE_INFO))
                    {
                        m_fs.last_write_time(path, now, VCPKG_LINE_INFO);
                    }
#endif
                    Debug::print("Restored ", zip_path.path, '\n');
                    out_status[i] = RestoreResult::restored;
                }
                else
                {
                    Debug::print("Failed to decompress archive package: ", zip_path.path, '\n');
                }

                post_decompress(zip_path);
            }
        }

        void post_decompress(const ZipResource& r) const
        {
            if (r.to_remove == RemoveWhen::always)
            {
                m_fs.remove(r.path, IgnoreErrors{});
            }
        }

        // For every action denoted by actions, at corresponding indicies in out_zips, stores a ZipResource indicating
        // the downloaded location.
        //
        // Leaving an Optional disengaged indicates that the cache does not contain the requested zip.
        virtual void acquire_zips(View<const InstallPlanAction*> actions,
                                  Span<Optional<ZipResource>> out_zips) const = 0;

    protected:
        ZipTool m_zip;
        const Filesystem& m_fs;
    };

    struct FilesReadBinaryProvider : ZipReadBinaryProvider
    {
        FilesReadBinaryProvider(ZipTool zip, const Filesystem& fs, Path&& dir)
            : ZipReadBinaryProvider(std::move(zip), fs), m_dir(std::move(dir))
        {
        }

        void acquire_zips(View<const InstallPlanAction*> actions,
                          Span<Optional<ZipResource>> out_zip_paths) const override
        {
            for (size_t i = 0; i < actions.size(); ++i)
            {
                const auto& abi_tag = actions[i]->package_abi().value_or_exit(VCPKG_LINE_INFO);
                auto archive_path = m_dir / files_archive_subpath(abi_tag);
                if (m_fs.exists(archive_path, IgnoreErrors{}))
                {
                    out_zip_paths[i].emplace(std::move(archive_path), RemoveWhen::nothing);
                }
            }
        }

        void precheck(View<const InstallPlanAction*> actions, Span<CacheAvailability> cache_status) const override
        {
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                const auto& action = *actions[idx];
                const auto& abi_tag = action.package_abi().value_or_exit(VCPKG_LINE_INFO);

                bool any_available = false;
                if (m_fs.exists(m_dir / files_archive_subpath(abi_tag), IgnoreErrors{}))
                {
                    any_available = true;
                }

                cache_status[idx] = any_available ? CacheAvailability::available : CacheAvailability::unavailable;
            }
        }
        LocalizedString restored_message(size_t count,
                                         std::chrono::high_resolution_clock::duration elapsed) const override
        {
            return msg::format(msgRestoredPackagesFromFiles,
                               msg::count = count,
                               msg::elapsed = ElapsedTime(elapsed),
                               msg::path = m_dir);
        }

    private:
        Path m_dir;
    };

    struct HTTPPutBinaryProvider : IWriteBinaryProvider
    {
        HTTPPutBinaryProvider(std::vector<UrlTemplate>&& urls, const std::vector<std::string>& secrets)
            : m_urls(std::move(urls)), m_secrets(secrets)
        {
        }

        size_t push_success(const BinaryPackageWriteInfo& request, MessageSink& msg_sink) override
        {
            if (!request.zip_path) return 0;
            const auto& zip_path = *request.zip_path.get();
            size_t count_stored = 0;
            for (auto&& templ : m_urls)
            {
                auto url = templ.instantiate_variables(request);
                PrintingDiagnosticContext pdc{msg_sink};
                WarningDiagnosticContext wdc{pdc};
                auto maybe_success =
                    store_to_asset_cache(wdc, url, SanitizedUrl{url, m_secrets}, "PUT", templ.headers, zip_path);
                if (maybe_success)
                {
                    count_stored++;
                }
            }
            return count_stored;
        }

        bool needs_nuspec_data() const override { return false; }
        bool needs_zip_file() const override { return true; }

    private:
        std::vector<UrlTemplate> m_urls;
        std::vector<std::string> m_secrets;
    };

    struct HttpGetBinaryProvider : ZipReadBinaryProvider
    {
        HttpGetBinaryProvider(ZipTool zip,
                              const Filesystem& fs,
                              const Path& buildtrees,
                              UrlTemplate&& url_template,
                              const std::vector<std::string>& secrets)
            : ZipReadBinaryProvider(std::move(zip), fs)
            , m_buildtrees(buildtrees)
            , m_url_template(std::move(url_template))
            , m_secrets(secrets)
        {
        }

        void acquire_zips(View<const InstallPlanAction*> actions,
                          Span<Optional<ZipResource>> out_zip_paths) const override
        {
            std::vector<std::pair<std::string, Path>> url_paths;
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                auto&& action = *actions[idx];
                auto read_info = BinaryPackageReadInfo{action};
                url_paths.emplace_back(m_url_template.instantiate_variables(read_info),
                                       make_temp_archive_path(m_buildtrees, read_info.spec, read_info.package_abi));
            }

            WarningDiagnosticContext wdc{console_diagnostic_context};
            auto codes = download_files_no_cache(wdc, url_paths, m_url_template.headers, m_secrets);
            for (size_t i = 0; i < codes.size(); ++i)
            {
                if (codes[i] == 200)
                {
                    out_zip_paths[i].emplace(std::move(url_paths[i].second), RemoveWhen::always);
                }
            }
        }

        void precheck(View<const InstallPlanAction*> actions, Span<CacheAvailability> out_status) const override
        {
            std::vector<std::string> urls;
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                urls.push_back(m_url_template.instantiate_variables(BinaryPackageReadInfo{*actions[idx]}));
            }

            WarningDiagnosticContext wdc{console_diagnostic_context};
            auto codes = url_heads(wdc, urls, {}, m_secrets);
            for (size_t i = 0; i < codes.size(); ++i)
            {
                out_status[i] = codes[i] == 200 ? CacheAvailability::available : CacheAvailability::unavailable;
            }

            for (size_t i = codes.size(); i < out_status.size(); ++i)
            {
                out_status[i] = CacheAvailability::unavailable;
            }
        }

        LocalizedString restored_message(size_t count,
                                         std::chrono::high_resolution_clock::duration elapsed) const override
        {
            return msg::format(msgRestoredPackagesFromHTTP, msg::count = count, msg::elapsed = ElapsedTime(elapsed));
        }

        Path m_buildtrees;
        UrlTemplate m_url_template;
        std::vector<std::string> m_secrets;
    };

    struct AzureBlobPutBinaryProvider : IWriteBinaryProvider
    {
        AzureBlobPutBinaryProvider(const Filesystem& fs,
                                   std::vector<UrlTemplate>&& urls,
                                   const std::vector<std::string>& secrets)
            : m_fs(fs), m_urls(std::move(urls)), m_secrets(secrets)
        {
        }

        size_t push_success(const BinaryPackageWriteInfo& request, MessageSink& msg_sink) override
        {
            if (!request.zip_path) return 0;

            const auto& zip_path = *request.zip_path.get();

            size_t count_stored = 0;
            const auto file_size = m_fs.file_size(zip_path, VCPKG_LINE_INFO);
            if (file_size == 0) return count_stored;

            // cf.
            // https://learn.microsoft.com/en-us/rest/api/storageservices/understanding-block-blobs--append-blobs--and-page-blobs?toc=%2Fazure%2Fstorage%2Fblobs%2Ftoc.json
            constexpr size_t max_single_write = 5000000000;
            bool use_azcopy = file_size > max_single_write;

            PrintingDiagnosticContext pdc{msg_sink};
            WarningDiagnosticContext wdc{pdc};

            for (auto&& templ : m_urls)
            {
                auto url = templ.instantiate_variables(request);
                auto maybe_success =
                    use_azcopy
                        ? azcopy_to_asset_cache(wdc, url, SanitizedUrl{url, m_secrets}, zip_path)
                        : store_to_asset_cache(wdc, url, SanitizedUrl{url, m_secrets}, "PUT", templ.headers, zip_path);
                if (maybe_success)
                {
                    count_stored++;
                }
            }
            return count_stored;
        }

        bool needs_nuspec_data() const override { return false; }
        bool needs_zip_file() const override { return true; }

    private:
        const Filesystem& m_fs;
        std::vector<UrlTemplate> m_urls;
        std::vector<std::string> m_secrets;
    };

    struct NuGetSource
    {
        StringLiteral option;
        std::string value;
    };

    NuGetSource nuget_sources_arg(View<std::string> sources) { return {"-Source", Strings::join(";", sources)}; }
    NuGetSource nuget_configfile_arg(const Path& config_path) { return {"-ConfigFile", config_path.native()}; }

    struct NuGetTool
    {
        NuGetTool(const ToolCache& cache, MessageSink& sink, const BinaryConfigParserState& shared)
            : m_timeout(shared.nugettimeout)
            , m_interactive(shared.nuget_interactive)
            , m_use_nuget_cache(shared.use_nuget_cache)
        {
#ifndef _WIN32
            m_cmd.string_arg(cache.get_tool_path(Tools::MONO, sink));
#endif
            m_cmd.string_arg(cache.get_tool_path(Tools::NUGET, sink));
        }

        ExpectedL<Unit> push(MessageSink& sink, const Path& nupkg_path, const NuGetSource& src) const
        {
            return run_nuget_commandline(push_cmd(nupkg_path, src), sink);
        }
        ExpectedL<Unit> pack(MessageSink& sink, const Path& nuspec_path, const Path& out_dir) const
        {
            return run_nuget_commandline(pack_cmd(nuspec_path, out_dir), sink);
        }
        ExpectedL<Unit> install(MessageSink& sink,
                                StringView packages_config,
                                const Path& out_dir,
                                const NuGetSource& src) const
        {
            return run_nuget_commandline(install_cmd(packages_config, out_dir, src), sink);
        }

    private:
        Command subcommand(StringLiteral sub) const
        {
            auto cmd = m_cmd;
            cmd.string_arg(sub).string_arg("-ForceEnglishOutput").string_arg("-Verbosity").string_arg("detailed");
            if (!m_interactive) cmd.string_arg("-NonInteractive");
            return cmd;
        }

        Command install_cmd(StringView packages_config, const Path& out_dir, const NuGetSource& src) const
        {
            auto cmd = subcommand("install");
            cmd.string_arg(packages_config)
                .string_arg("-OutputDirectory")
                .string_arg(out_dir)
                .string_arg("-ExcludeVersion")
                .string_arg("-PreRelease")
                .string_arg("-PackageSaveMode")
                .string_arg("nupkg");
            if (!m_use_nuget_cache) cmd.string_arg("-DirectDownload").string_arg("-NoCache");
            cmd.string_arg(src.option).string_arg(src.value);
            return cmd;
        }

        Command pack_cmd(const Path& nuspec_path, const Path& out_dir) const
        {
            return subcommand("pack")
                .string_arg(nuspec_path)
                .string_arg("-OutputDirectory")
                .string_arg(out_dir)
                .string_arg("-NoDefaultExcludes");
        }

        Command push_cmd(const Path& nupkg_path, const NuGetSource& src) const
        {
            return subcommand("push")
                .string_arg(nupkg_path)
                .string_arg("-Timeout")
                .string_arg(m_timeout)
                .string_arg(src.option)
                .string_arg(src.value);
        }

        ExpectedL<Unit> run_nuget_commandline(const Command& cmd, MessageSink& msg_sink) const
        {
            if (m_interactive)
            {
                return cmd_execute(cmd).then([](int exit_code) -> ExpectedL<Unit> {
                    if (exit_code == 0)
                    {
                        return {Unit{}};
                    }

                    return msg::format_error(msgNugetOutputNotCapturedBecauseInteractiveSpecified);
                });
            }

            RedirectedProcessLaunchSettings show_in_debug_settings;
            show_in_debug_settings.echo_in_debug = EchoInDebug::Show;
            return cmd_execute_and_capture_output(cmd, show_in_debug_settings)
                .then([&](ExitCodeAndOutput&& res) -> ExpectedL<Unit> {
                    if (res.output.find("Authentication may require manual action.") != std::string::npos)
                    {
                        msg_sink.println(
                            Color::warning, msgAuthenticationMayRequireManualAction, msg::vendor = "Nuget");
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
                                         msg::url = docs::troubleshoot_binary_cache_url);
                    }
                    else if (res.output.find("for example \"-ApiKey AzureDevOps\"") != std::string::npos)
                    {
                        auto real_cmd = cmd;
                        real_cmd.string_arg("-ApiKey").string_arg("AzureDevOps");
                        return cmd_execute_and_capture_output(real_cmd, show_in_debug_settings)
                            .then([&](ExitCodeAndOutput&& res) -> ExpectedL<Unit> {
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

        Command m_cmd;
        std::string m_timeout;
        bool m_interactive;
        bool m_use_nuget_cache;
    };

    struct NugetBaseBinaryProvider
    {
        NugetBaseBinaryProvider(const Filesystem& fs,
                                const NuGetTool& tool,
                                const Path& packages,
                                const Path& buildtrees,
                                StringView nuget_prefix)
            : m_fs(fs)
            , m_cmd(tool)
            , m_packages(packages)
            , m_buildtrees(buildtrees)
            , m_nuget_prefix(nuget_prefix.to_string())
        {
        }

        const Filesystem& m_fs;
        NuGetTool m_cmd;
        Path m_packages;
        Path m_buildtrees;
        std::string m_nuget_prefix;
    };

    struct NugetReadBinaryProvider : IReadBinaryProvider, private NugetBaseBinaryProvider
    {
        NugetReadBinaryProvider(const NugetBaseBinaryProvider& base, NuGetSource src)
            : NugetBaseBinaryProvider(base), m_src(std::move(src))
        {
        }

        NuGetSource m_src;

        static std::string generate_packages_config(View<FeedReference> refs)
        {
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

        // Prechecking is too expensive with NuGet, so it is not implemented
        void precheck(View<const InstallPlanAction*>, Span<CacheAvailability>) const override { }

        LocalizedString restored_message(size_t count,
                                         std::chrono::high_resolution_clock::duration elapsed) const override
        {
            return msg::format(msgRestoredPackagesFromNuGet, msg::count = count, msg::elapsed = ElapsedTime(elapsed));
        }

        void fetch(View<const InstallPlanAction*> actions, Span<RestoreResult> out_status) const override
        {
            auto packages_config = m_buildtrees / "packages.config";
            auto refs =
                Util::fmap(actions, [this](const InstallPlanAction* p) { return make_nugetref(*p, m_nuget_prefix); });
            m_fs.write_contents(packages_config, generate_packages_config(refs), VCPKG_LINE_INFO);
            m_cmd.install(out_sink, packages_config, m_packages, m_src);
            for (size_t i = 0; i < actions.size(); ++i)
            {
                // nuget.exe provides the nupkg file and the unpacked folder
                const auto nupkg_path = m_packages / refs[i].id / refs[i].id + ".nupkg";
                if (m_fs.exists(nupkg_path, IgnoreErrors{}))
                {
                    m_fs.remove(nupkg_path, VCPKG_LINE_INFO);
                    const auto nuget_dir = actions[i]->spec.dir();
                    if (nuget_dir != refs[i].id)
                    {
                        const auto path_from = m_packages / refs[i].id;
                        const auto path_to = m_packages / nuget_dir;
                        m_fs.rename(path_from, path_to, VCPKG_LINE_INFO);
                    }

                    out_status[i] = RestoreResult::restored;
                }
            }
        }
    };

    struct NugetBinaryPushProvider : IWriteBinaryProvider, private NugetBaseBinaryProvider
    {
        NugetBinaryPushProvider(const NugetBaseBinaryProvider& base,
                                std::vector<std::string>&& sources,
                                std::vector<Path>&& configs)
            : NugetBaseBinaryProvider(base), m_sources(std::move(sources)), m_configs(std::move(configs))
        {
        }

        std::vector<std::string> m_sources;
        std::vector<Path> m_configs;

        bool needs_nuspec_data() const override { return true; }
        bool needs_zip_file() const override { return false; }

        size_t push_success(const BinaryPackageWriteInfo& request, MessageSink& msg_sink) override
        {
            auto& spec = request.spec;

            auto nuspec_path = m_buildtrees / spec.name() / spec.triplet().canonical_name() + ".nuspec";
            std::error_code ec;
            m_fs.write_contents(nuspec_path, request.nuspec.value_or_exit(VCPKG_LINE_INFO), ec);
            if (ec)
            {
                msg_sink.println(Color::error, msgPackingVendorFailed, msg::vendor = "NuGet");
                return 0;
            }

            auto packed_result = m_cmd.pack(msg_sink, nuspec_path, m_buildtrees);
            m_fs.remove(nuspec_path, IgnoreErrors{});
            if (!packed_result)
            {
                msg_sink.println(Color::error, msgPackingVendorFailed, msg::vendor = "NuGet");
                return 0;
            }

            size_t count_stored = 0;
            auto nupkg_path = m_buildtrees / make_feedref(request, m_nuget_prefix).nupkg_filename();
            for (auto&& write_src : m_sources)
            {
                msg_sink.println(msgUploadingBinariesToVendor,
                                 msg::spec = request.display_name,
                                 msg::vendor = "NuGet",
                                 msg::path = write_src);
                if (!m_cmd.push(msg_sink, nupkg_path, nuget_sources_arg({&write_src, 1})))
                {
                    msg_sink.println(Color::error,
                                     msg::format(msgPushingVendorFailed, msg::vendor = "NuGet", msg::path = write_src)
                                         .append_raw('\n')
                                         .append(msgSeeURL, msg::url = docs::troubleshoot_binary_cache_url));
                }
                else
                {
                    count_stored++;
                }
            }
            for (auto&& write_cfg : m_configs)
            {
                msg_sink.println(msgUploadingBinariesToVendor,
                                 msg::spec = spec,
                                 msg::vendor = "NuGet config",
                                 msg::path = write_cfg);
                if (!m_cmd.push(msg_sink, nupkg_path, nuget_configfile_arg(write_cfg)))
                {
                    msg_sink.println(
                        Color::error,
                        msg::format(msgPushingVendorFailed, msg::vendor = "NuGet config", msg::path = write_cfg)
                            .append_raw('\n')
                            .append(msgSeeURL, msg::url = docs::troubleshoot_binary_cache_url));
                }
                else
                {
                    count_stored++;
                }
            }

            m_fs.remove(nupkg_path, IgnoreErrors{});
            return count_stored;
        }
    };

    template<class ResultOnSuccessType>
    static ExpectedL<ResultOnSuccessType> flatten_generic(const ExpectedL<ExitCodeAndOutput>& maybe_exit,
                                                          StringView tool_name,
                                                          ResultOnSuccessType result_on_success)
    {
        if (auto exit = maybe_exit.get())
        {
            if (exit->exit_code == 0)
            {
                return {result_on_success};
            }

            return {msg::format_error(
                        msgProgramReturnedNonzeroExitCode, msg::tool_name = tool_name, msg::exit_code = exit->exit_code)
                        .append_raw('\n')
                        .append_raw(exit->output)};
        }

        return {msg::format_error(msgLaunchingProgramFailed, msg::tool_name = tool_name)
                    .append_raw(' ')
                    .append_raw(maybe_exit.error().to_string())};
    }

    struct IObjectStorageTool
    {
        virtual ~IObjectStorageTool() = default;

        virtual LocalizedString restored_message(size_t count,
                                                 std::chrono::high_resolution_clock::duration elapsed) const = 0;
        virtual ExpectedL<CacheAvailability> stat(StringView url) const = 0;
        virtual ExpectedL<RestoreResult> download_file(StringView object, const Path& archive) const = 0;
        virtual ExpectedL<Unit> upload_file(StringView object, const Path& archive) const = 0;
    };

    struct ObjectStorageProvider : ZipReadBinaryProvider
    {
        ObjectStorageProvider(ZipTool zip,
                              const Filesystem& fs,
                              const Path& buildtrees,
                              std::string&& prefix,
                              const std::shared_ptr<const IObjectStorageTool>& tool)
            : ZipReadBinaryProvider(std::move(zip), fs)
            , m_buildtrees(buildtrees)
            , m_prefix(std::move(prefix))
            , m_tool(tool)
        {
        }

        static std::string make_object_path(const std::string& prefix, const std::string& abi)
        {
            return Strings::concat(prefix, abi, ".zip");
        }

        void acquire_zips(View<const InstallPlanAction*> actions,
                          Span<Optional<ZipResource>> out_zip_paths) const override
        {
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                auto&& action = *actions[idx];
                const auto& abi = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
                auto tmp = make_temp_archive_path(m_buildtrees, action.spec, abi);
                auto res = m_tool->download_file(make_object_path(m_prefix, abi), tmp);
                if (auto cache_result = res.get())
                {
                    if (*cache_result == RestoreResult::restored)
                    {
                        out_zip_paths[idx].emplace(std::move(tmp), RemoveWhen::always);
                    }
                }
                else
                {
                    msg::println_warning(res.error());
                }
            }
        }

        void precheck(View<const InstallPlanAction*> actions, Span<CacheAvailability> cache_status) const override
        {
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                auto&& action = *actions[idx];
                const auto& abi = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
                auto maybe_res = m_tool->stat(make_object_path(m_prefix, abi));
                if (auto res = maybe_res.get())
                {
                    cache_status[idx] = *res;
                }
                else
                {
                    cache_status[idx] = CacheAvailability::unavailable;
                }
            }
        }

        LocalizedString restored_message(size_t count,
                                         std::chrono::high_resolution_clock::duration elapsed) const override
        {
            return m_tool->restored_message(count, elapsed);
        }

        Path m_buildtrees;
        std::string m_prefix;
        std::shared_ptr<const IObjectStorageTool> m_tool;
    };
    struct ObjectStoragePushProvider : IWriteBinaryProvider
    {
        ObjectStoragePushProvider(std::vector<std::string>&& prefixes, std::shared_ptr<const IObjectStorageTool> tool)
            : m_prefixes(std::move(prefixes)), m_tool(std::move(tool))
        {
        }

        static std::string make_object_path(const std::string& prefix, const std::string& abi)
        {
            return Strings::concat(prefix, abi, ".zip");
        }

        size_t push_success(const BinaryPackageWriteInfo& request, MessageSink& msg_sink) override
        {
            if (!request.zip_path) return 0;
            const auto& zip_path = *request.zip_path.get();
            size_t upload_count = 0;
            for (const auto& prefix : m_prefixes)
            {
                auto res = m_tool->upload_file(make_object_path(prefix, request.package_abi), zip_path);
                if (res)
                {
                    ++upload_count;
                }
                else
                {
                    msg_sink.println(warning_prefix().append(std::move(res).error()));
                }
            }
            return upload_count;
        }

        bool needs_nuspec_data() const override { return false; }
        bool needs_zip_file() const override { return true; }

        std::vector<std::string> m_prefixes;
        std::shared_ptr<const IObjectStorageTool> m_tool;
    };

    struct AzCopyStorageProvider : ZipReadBinaryProvider
    {
        AzCopyStorageProvider(
            ZipTool zip, const Filesystem& fs, const Path& buildtrees, AzCopyUrl&& az_url, const Path& tool)
            : ZipReadBinaryProvider(std::move(zip), fs)
            , m_buildtrees(buildtrees)
            , m_url(std::move(az_url))
            , m_tool(tool)
        {
        }

        // Batch the azcopy arguments to fit within the maximum allowed command line length.
        static std::vector<std::vector<std::string>> batch_azcopy_args(const std::vector<std::string>& abis,
                                                                       const size_t reserved_len)
        {
            return batch_command_arguments_with_fixed_length(abis,
                                                             reserved_len,
                                                             Command::maximum_allowed,
                                                             ABI_LENGTH + 4, // ABI_LENGTH for SHA256 + 4 for ".zip"
                                                             1);             // the separator length is 1 for ';'
        }

        std::vector<std::string> azcopy_list() const
        {
            auto maybe_output = cmd_execute_and_capture_output(Command{m_tool}
                                                                   .string_arg("list")
                                                                   .string_arg("--output-level")
                                                                   .string_arg("ESSENTIAL")
                                                                   .string_arg(m_url.make_container_path()));

            auto output = maybe_output.get();
            if (!output)
            {
                msg::println_warning(maybe_output.error());
                return {};
            }

            if (output->exit_code != 0)
            {
                msg::println_warning(LocalizedString::from_raw(output->output));
                return {};
            }

            std::vector<std::string> abis;
            for (const auto& line : Strings::split(output->output, '\n'))
            {
                if (line.empty()) continue;
                // `azcopy list` output uses format `<filename>; Content Length: <size>`, we only need the filename
                auto first_part_end = std::find(line.begin(), line.end(), ';');
                if (first_part_end != line.end())
                {
                    std::string abifile{line.begin(), first_part_end};

                    // Check file names with the format `<abi>.zip`
                    if (abifile.size() == ABI_LENGTH + 4 &&
                        std::all_of(abifile.begin(), abifile.begin() + ABI_LENGTH, ParserBase::is_hex_digit) &&
                        abifile.substr(ABI_LENGTH) == ".zip")
                    {
                        // remove ".zip" extension
                        abis.emplace_back(abifile.substr(0, abifile.size() - 4));
                    }
                }
            }
            return abis;
        }

        void acquire_zips(View<const InstallPlanAction*> actions,
                          Span<Optional<ZipResource>> out_zip_paths) const override
        {
            std::vector<std::string> abis;
            std::map<std::string, size_t> abi_index_map;
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                auto&& action = *actions[idx];
                const auto& abi = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
                abis.push_back(abi);
                abi_index_map[abi] = idx;
            }

            const auto tmp_downloads_location = m_buildtrees / ".azcopy";
            auto base_cmd = Command{m_tool}
                                .string_arg("copy")
                                .string_arg("--from-to")
                                .string_arg("BlobLocal")
                                .string_arg("--output-level")
                                .string_arg("QUIET")
                                .string_arg("--overwrite")
                                .string_arg("true")
                                .string_arg(m_url.make_container_path())
                                .string_arg(tmp_downloads_location)
                                .string_arg("--include-path");

            const size_t reserved_len =
                base_cmd.command_line().size() + 4; // for space + surrounding quotes + terminator
            for (auto&& batch : batch_azcopy_args(abis, reserved_len))
            {
                auto maybe_output = cmd_execute_and_capture_output(Command{base_cmd}.string_arg(
                    Strings::join(";", Util::fmap(batch, [](const auto& abi) { return abi + ".zip"; }))));
                // We don't return on a failure because the command may have
                // only failed to restore some of the requested packages.
                if (!maybe_output.has_value())
                {
                    msg::println_warning(maybe_output.error());
                }
            }

            const auto& container_url = m_url.url;
            const auto last_slash = std::find(container_url.rbegin(), container_url.rend(), '/');
            const auto container_name = std::string{last_slash.base(), container_url.end()};
            for (auto&& file : m_fs.get_files_non_recursive(tmp_downloads_location / container_name, VCPKG_LINE_INFO))
            {
                auto filename = file.stem().to_string();
                auto it = abi_index_map.find(filename);
                if (it != abi_index_map.end())
                {
                    out_zip_paths[it->second].emplace(std::move(file), RemoveWhen::always);
                }
            }
        }

        void precheck(View<const InstallPlanAction*> actions, Span<CacheAvailability> cache_status) const override
        {
            auto abis = azcopy_list();
            if (abis.empty())
            {
                // If the command failed, we assume that the cache is unavailable.
                std::fill(cache_status.begin(), cache_status.end(), CacheAvailability::unavailable);
                return;
            }

            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                auto&& action = *actions[idx];
                const auto& abi = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
                cache_status[idx] =
                    Util::contains(abis, abi) ? CacheAvailability::available : CacheAvailability::unavailable;
            }
        }

        LocalizedString restored_message(size_t count,
                                         std::chrono::high_resolution_clock::duration elapsed) const override
        {
            return msg::format(
                msgRestoredPackagesFromAzureStorage, msg::count = count, msg::elapsed = ElapsedTime(elapsed));
        }

        Path m_buildtrees;
        AzCopyUrl m_url;
        Path m_tool;
    };
    struct AzCopyStoragePushProvider : IWriteBinaryProvider
    {
        AzCopyStoragePushProvider(std::vector<AzCopyUrl>&& containers, const Path& tool)
            : m_containers(std::move(containers)), m_tool(tool)
        {
        }

        vcpkg::ExpectedL<Unit> upload_file(StringView url, const Path& archive) const
        {
            auto upload_cmd = Command{m_tool}
                                  .string_arg("copy")
                                  .string_arg("--from-to")
                                  .string_arg("LocalBlob")
                                  .string_arg("--overwrite")
                                  .string_arg("true")
                                  .string_arg(archive)
                                  .string_arg(url);

            return flatten(cmd_execute_and_capture_output(upload_cmd), Tools::AZCOPY);
        }

        size_t push_success(const BinaryPackageWriteInfo& request, MessageSink& msg_sink) override
        {
            const auto& zip_path = request.zip_path.value_or_exit(VCPKG_LINE_INFO);
            size_t upload_count = 0;
            for (const auto& container : m_containers)
            {
                auto res = upload_file(container.make_object_path(request.package_abi), zip_path);
                if (res)
                {
                    ++upload_count;
                }
                else
                {
                    msg_sink.println(warning_prefix().append(std::move(res).error()));
                }
            }
            return upload_count;
        }

        bool needs_nuspec_data() const override { return false; }
        bool needs_zip_file() const override { return true; }

        std::vector<AzCopyUrl> m_containers;
        Path m_tool;
    };

    struct GcsStorageTool : IObjectStorageTool
    {
        GcsStorageTool(const ToolCache& cache, MessageSink& sink) : m_tool(cache.get_tool_path(Tools::GSUTIL, sink)) { }

        LocalizedString restored_message(size_t count,
                                         std::chrono::high_resolution_clock::duration elapsed) const override
        {
            return msg::format(msgRestoredPackagesFromGCS, msg::count = count, msg::elapsed = ElapsedTime(elapsed));
        }

        ExpectedL<CacheAvailability> stat(StringView url) const override
        {
            return flatten_generic(
                cmd_execute_and_capture_output(Command{m_tool}.string_arg("-q").string_arg("stat").string_arg(url)),
                Tools::GSUTIL,
                CacheAvailability::available);
        }

        ExpectedL<RestoreResult> download_file(StringView object, const Path& archive) const override
        {
            return flatten_generic(
                cmd_execute_and_capture_output(
                    Command{m_tool}.string_arg("-q").string_arg("cp").string_arg(object).string_arg(archive)),
                Tools::GSUTIL,
                RestoreResult::restored);
        }

        ExpectedL<Unit> upload_file(StringView object, const Path& archive) const override
        {
            return flatten(
                cmd_execute_and_capture_output(
                    Command{m_tool}.string_arg("-q").string_arg("cp").string_arg(archive).string_arg(object)),
                Tools::GSUTIL);
        }

        Path m_tool;
    };

    struct AwsStorageTool : IObjectStorageTool
    {
        AwsStorageTool(const ToolCache& cache, MessageSink& sink, bool no_sign_request)
            : m_tool(cache.get_tool_path(Tools::AWSCLI, sink)), m_no_sign_request(no_sign_request)
        {
        }

        LocalizedString restored_message(size_t count,
                                         std::chrono::high_resolution_clock::duration elapsed) const override
        {
            return msg::format(msgRestoredPackagesFromAWS, msg::count = count, msg::elapsed = ElapsedTime(elapsed));
        }

        ExpectedL<CacheAvailability> stat(StringView url) const override
        {
            auto cmd = Command{m_tool}.string_arg("s3").string_arg("ls").string_arg(url);
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }

            auto maybe_exit = cmd_execute_and_capture_output(cmd);

            // When the file is not found, "aws s3 ls" prints nothing, and returns exit code 1.
            // flatten_generic() would treat this as an error, but we want to treat it as a (silent) cache miss
            // instead, so we handle this special case before calling flatten_generic(). See
            // https://github.com/aws/aws-cli/issues/5544 for the related aws-cli bug report.
            if (auto exit = maybe_exit.get())
            {
                // We want to return CacheAvailability::unavailable even if aws-cli starts to return exit code 0
                // with an empty output when the file is missing. This way, both the current and possible future
                // behavior of aws-cli is covered.
                if (exit->exit_code == 0 || exit->exit_code == 1)
                {
                    if (Strings::trim(exit->output).empty())
                    {
                        return CacheAvailability::unavailable;
                    }
                }
            }

            // In the non-special case, simply let flatten_generic() do its job.
            return flatten_generic(maybe_exit, Tools::AWSCLI, CacheAvailability::available);
        }

        ExpectedL<RestoreResult> download_file(StringView object, const Path& archive) const override
        {
            auto r = stat(object);
            if (auto stat_result = r.get())
            {
                if (*stat_result != CacheAvailability::available)
                {
                    return RestoreResult::unavailable;
                }
            }
            else
            {
                return r.error();
            }

            auto cmd = Command{m_tool}.string_arg("s3").string_arg("cp").string_arg(object).string_arg(archive);
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }

            return flatten_generic(cmd_execute_and_capture_output(cmd), Tools::AWSCLI, RestoreResult::restored);
        }

        ExpectedL<Unit> upload_file(StringView object, const Path& archive) const override
        {
            auto cmd = Command{m_tool}.string_arg("s3").string_arg("cp").string_arg(archive).string_arg(object);
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }
            return flatten(cmd_execute_and_capture_output(cmd), Tools::AWSCLI);
        }

        Path m_tool;
        bool m_no_sign_request;
    };

    struct CosStorageTool : IObjectStorageTool
    {
        CosStorageTool(const ToolCache& cache, MessageSink& sink) : m_tool(cache.get_tool_path(Tools::COSCLI, sink)) { }

        LocalizedString restored_message(size_t count,
                                         std::chrono::high_resolution_clock::duration elapsed) const override
        {
            return msg::format(msgRestoredPackagesFromCOS, msg::count = count, msg::elapsed = ElapsedTime(elapsed));
        }

        ExpectedL<CacheAvailability> stat(StringView url) const override
        {
            return flatten_generic(cmd_execute_and_capture_output(Command{m_tool}.string_arg("ls").string_arg(url)),
                                   Tools::COSCLI,
                                   CacheAvailability::available);
        }

        ExpectedL<RestoreResult> download_file(StringView object, const Path& archive) const override
        {
            return flatten_generic(
                cmd_execute_and_capture_output(Command{m_tool}.string_arg("cp").string_arg(object).string_arg(archive)),
                Tools::COSCLI,
                RestoreResult::restored);
        }

        ExpectedL<Unit> upload_file(StringView object, const Path& archive) const override
        {
            return flatten(
                cmd_execute_and_capture_output(Command{m_tool}.string_arg("cp").string_arg(archive).string_arg(object)),
                Tools::COSCLI);
        }

        Path m_tool;
    };

    struct AzureUpkgTool
    {
        AzureUpkgTool(const ToolCache& cache, MessageSink& sink) { az_cli = cache.get_tool_path(Tools::AZCLI, sink); }

        Command base_cmd(const AzureUpkgSource& src,
                         StringView package_name,
                         StringView package_version,
                         StringView verb) const
        {
            Command cmd{az_cli};
            cmd.string_arg("artifacts")
                .string_arg("universal")
                .string_arg(verb)
                .string_arg("--organization")
                .string_arg(src.organization)
                .string_arg("--feed")
                .string_arg(src.feed)
                .string_arg("--name")
                .string_arg(package_name)
                .string_arg("--version")
                .string_arg(package_version);
            if (!src.project.empty())
            {
                cmd.string_arg("--project").string_arg(src.project).string_arg("--scope").string_arg("project");
            }
            return cmd;
        }

        ExpectedL<Unit> download(const AzureUpkgSource& src,
                                 StringView package_name,
                                 StringView package_version,
                                 const Path& download_path,
                                 MessageSink& sink) const
        {
            Command cmd = base_cmd(src, package_name, package_version, "download");
            cmd.string_arg("--path").string_arg(download_path);
            return run_az_artifacts_cmd(cmd, sink);
        }

        ExpectedL<Unit> publish(const AzureUpkgSource& src,
                                StringView package_name,
                                StringView package_version,
                                const Path& zip_path,
                                StringView description,
                                MessageSink& sink) const
        {
            Command cmd = base_cmd(src, package_name, package_version, "publish");
            cmd.string_arg("--description").string_arg(description).string_arg("--path").string_arg(zip_path);
            return run_az_artifacts_cmd(cmd, sink);
        }

        ExpectedL<Unit> run_az_artifacts_cmd(const Command& cmd, MessageSink& sink) const
        {
            RedirectedProcessLaunchSettings show_in_debug_settings;
            show_in_debug_settings.echo_in_debug = EchoInDebug::Show;
            return cmd_execute_and_capture_output(cmd, show_in_debug_settings)
                .then([&](ExitCodeAndOutput&& res) -> ExpectedL<Unit> {
                    if (res.exit_code == 0)
                    {
                        return {Unit{}};
                    }

                    // az command line error message: Before you can run Azure DevOps commands, you need to
                    // run the login command(az login if using AAD/MSA identity else az devops login if using PAT
                    // token) to setup credentials.
                    if (res.output.find("you need to run the login command") != std::string::npos)
                    {
                        sink.println(Color::warning,
                                     msgFailedVendorAuthentication,
                                     msg::vendor = "Universal Packages",
                                     msg::url = "https://learn.microsoft.com/cli/azure/authenticate-azure-cli");
                    }
                    return LocalizedString::from_raw(std::move(res).output);
                });
        }
        Path az_cli;
    };

    struct AzureUpkgPutBinaryProvider : public IWriteBinaryProvider
    {
        AzureUpkgPutBinaryProvider(const ToolCache& cache, MessageSink& sink, std::vector<AzureUpkgSource>&& sources)
            : m_azure_tool(cache, sink), m_sources(sources)
        {
        }

        size_t push_success(const BinaryPackageWriteInfo& request, MessageSink& msg_sink) override
        {
            size_t count_stored = 0;
            auto ref = make_feedref(request, "");
            std::string package_description = "Cached package for " + ref.id;

            const Path& zip_path = request.zip_path.value_or_exit(VCPKG_LINE_INFO);
            for (auto&& write_src : m_sources)
            {
                auto res =
                    m_azure_tool.publish(write_src, ref.id, ref.version, zip_path, package_description, msg_sink);
                if (res)
                {
                    count_stored++;
                }
                else
                {
                    msg_sink.println(res.error());
                }
            }

            return count_stored;
        }

        bool needs_nuspec_data() const override { return false; }
        bool needs_zip_file() const override { return true; }

    private:
        AzureUpkgTool m_azure_tool;
        std::vector<AzureUpkgSource> m_sources;
    };

    struct AzureUpkgGetBinaryProvider : public ZipReadBinaryProvider
    {
        AzureUpkgGetBinaryProvider(ZipTool zip,
                                   const Filesystem& fs,
                                   const ToolCache& cache,
                                   MessageSink& sink,
                                   AzureUpkgSource&& source,
                                   const Path& buildtrees)
            : ZipReadBinaryProvider(std::move(zip), fs)
            , m_azure_tool(cache, sink)
            , m_sink(sink)
            , m_source(std::move(source))
            , m_buildtrees(buildtrees)
        {
        }

        // Prechecking doesn't exist with universal packages so it's not implemented
        void precheck(View<const InstallPlanAction*>, Span<CacheAvailability>) const override { }

        LocalizedString restored_message(size_t count,
                                         std::chrono::high_resolution_clock::duration elapsed) const override
        {
            return msg::format(msgRestoredPackagesFromAZUPKG, msg::count = count, msg::elapsed = ElapsedTime(elapsed));
        }

        void acquire_zips(View<const InstallPlanAction*> actions, Span<Optional<ZipResource>> out_zips) const override
        {
            for (size_t i = 0; i < actions.size(); ++i)
            {
                const auto& action = *actions[i];
                const auto info = BinaryPackageReadInfo{action};
                const auto ref = make_feedref(info, "");

                Path temp_dir = m_buildtrees / fmt::format("upkg_download_{}", info.package_abi);
                Path temp_zip_path = temp_dir / fmt::format("{}.zip", ref.id);
                Path final_zip_path = m_buildtrees / fmt::format("{}.zip", ref.id);

                const auto result = m_azure_tool.download(m_source, ref.id, ref.version, temp_dir, m_sink);
                if (result.has_value() && m_fs.exists(temp_zip_path, IgnoreErrors{}))
                {
                    m_fs.rename(temp_zip_path, final_zip_path, VCPKG_LINE_INFO);
                    out_zips[i].emplace(std::move(final_zip_path), RemoveWhen::always);
                }
                else
                {
                    msg::println_warning(result.error());
                }

                if (m_fs.exists(temp_dir, IgnoreErrors{}))
                {
                    m_fs.remove(temp_dir, VCPKG_LINE_INFO);
                }
            }
        }

    private:
        AzureUpkgTool m_azure_tool;
        MessageSink& m_sink;
        AzureUpkgSource m_source;
        const Path& m_buildtrees;
    };

    ExpectedL<Path> default_cache_path_impl()
    {
        auto maybe_cachepath = get_environment_variable(EnvironmentVariableVcpkgDefaultBinaryCache);
        if (auto p_str = maybe_cachepath.get())
        {
            get_global_metrics_collector().track_define(DefineMetric::VcpkgDefaultBinaryCache);
            Path path = std::move(*p_str);
            path.make_preferred();
            if (!real_filesystem.is_directory(path))
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

    struct BinaryConfigParser : ConfigSegmentsParser
    {
        BinaryConfigParser(StringView text, Optional<StringView> origin, BinaryConfigParserState* state)
            : ConfigSegmentsParser(text, origin, {0, 0}), state(state)
        {
        }

        BinaryConfigParserState* state;

        void parse()
        {
            auto all_segments = parse_all_segments();
            for (auto&& x : all_segments)
            {
                if (messages().any_errors()) return;
                handle_segments(std::move(x));
            }
        }

    private:
        bool check_azure_base_url(const std::pair<SourceLoc, std::string>& candidate_segment,
                                  StringLiteral binary_source)
        {
            if (!Strings::starts_with(candidate_segment.second, "https://") &&
                // Allow unencrypted Azurite for testing (not reflected in error msg)
                !Strings::starts_with(candidate_segment.second, "http://127.0.0.1"))
            {
                add_error(msg::format(msgInvalidArgumentRequiresBaseUrl,
                                      msg::base_url = "https://",
                                      msg::binary_source = binary_source),
                          candidate_segment.first);
                return false;
            }

            return true;
        }

        void handle_azcopy_segments(const std::vector<std::pair<SourceLoc, std::string>>& segments)
        {
            // Scheme: x-azcopy,<baseurl>[,<readwrite>]
            if (segments.size() < 2)
            {
                add_error(msg::format(msgInvalidArgumentRequiresBaseUrl,
                                      msg::base_url = "https://",
                                      msg::binary_source = "x-azcopy"),
                          segments[0].first);
                return;
            }

            if (segments.size() > 3)
            {
                add_error(msg::format(msgInvalidArgumentRequiresOneOrTwoArguments, msg::binary_source = "x-azcopy"),
                          segments[3].first);
                return;
            }

            // handle base URL
            if (!check_azure_base_url(segments[1], "x-azcopy"))
            {
                return;
            }

            handle_readwrite(
                state->azcopy_read_templates, state->azcopy_write_templates, {segments[1].second, ""}, segments, 2);

            // We count azcopy and azcopy-sas as the same provider
            state->binary_cache_providers.insert("azcopy");
        }

        void handle_azcopy_sas_segments(const std::vector<std::pair<SourceLoc, std::string>>& segments)
        {
            // Scheme: x-azcopy-sas,<baseurl>,<sas>[,<readwrite>]
            if (segments.size() < 3)
            {
                add_error(msg::format(msgInvalidArgumentRequiresBaseUrlAndToken, msg::binary_source = "x-azcopy-sas"),
                          segments[0].first);
                return;
            }

            if (segments.size() > 4)
            {
                add_error(
                    msg::format(msgInvalidArgumentRequiresTwoOrThreeArguments, msg::binary_source = "x-azcopy-sas"),
                    segments[4].first);
                return;
            }

            if (!check_azure_base_url(segments[1], "x-azcopy-sas"))
            {
                return;
            }

            // handle SAS token
            const auto& sas = segments[2].second;
            if (sas.empty() || Strings::starts_with(sas, "?"))
            {
                return add_error(msg::format(msgInvalidArgumentRequiresValidToken, msg::binary_source = "x-azcopy-sas"),
                                 segments[2].first);
            }
            state->secrets.push_back(sas);

            handle_readwrite(
                state->azcopy_read_templates, state->azcopy_write_templates, {segments[1].second, sas}, segments, 3);

            // We count azcopy and azcopy-sas as the same provider
            state->binary_cache_providers.insert("azcopy-sas");
        }

        void handle_segments(std::vector<std::pair<SourceLoc, std::string>>&& segments)
        {
            Checks::check_exit(VCPKG_LINE_INFO, !segments.empty());
            if (segments[0].second == "clear")
            {
                if (segments.size() != 1)
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresNoneArguments, msg::binary_source = "clear"),
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

                handle_readwrite(state->archives_to_read, state->archives_to_write, std::move(p), segments, 2);
                if (segments.size() > 3)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresOneOrTwoArguments, msg::binary_source = "files"),
                        segments[3].first);
                }
                state->binary_cache_providers.insert("files");
            }
            else if (segments[0].second == "interactive")
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
                state->binary_cache_providers.insert("nuget");
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
                state->binary_cache_providers.insert("nuget");
            }
            else if (segments[0].second == "nugettimeout")
            {
                if (segments.size() != 2)
                {
                    return add_error(msg::format(msgNugetTimeoutExpectsSinglePositiveInteger));
                }

                long timeout = Strings::strto<long>(segments[1].second).value_or(-1);
                if (timeout <= 0)
                {
                    return add_error(msg::format(msgNugetTimeoutExpectsSinglePositiveInteger));
                }

                state->nugettimeout = std::to_string(timeout);
                state->binary_cache_providers.insert("nuget");
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

                handle_readwrite(
                    state->archives_to_read, state->archives_to_write, Path(*maybe_home.get()), segments, 1);
                state->binary_cache_providers.insert("default");
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

                if (!check_azure_base_url(segments[1], "azblob"))
                {
                    return;
                }

                // <url>/{sha}.zip[?<sas>]
                AzCopyUrl p;
                p.url = segments[1].second;

                const auto& sas = segments[2].second;
                if (sas.empty() || Strings::starts_with(sas, "?"))
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresValidToken, msg::binary_source = "azblob"),
                                     segments[2].first);
                }
                state->secrets.push_back(sas);
                p.sas = sas;

                if (segments.size() > 4)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresTwoOrThreeArguments, msg::binary_source = "azblob"),
                        segments[4].first);
                }

                UrlTemplate url_template = {p.make_object_path("{sha}")};
                bool read = false, write = false;
                handle_readwrite(read, write, segments, 3);
                if (read) state->url_templates_to_get.push_back(url_template);
                auto headers = azure_blob_headers();
                url_template.headers.assign(headers.begin(), headers.end());
                if (write) state->azblob_templates_to_put.push_back(url_template);

                state->binary_cache_providers.insert("azblob");
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

                handle_readwrite(state->gcs_read_prefixes, state->gcs_write_prefixes, std::move(p), segments, 2);

                state->binary_cache_providers.insert("gcs");
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

                handle_readwrite(state->aws_read_prefixes, state->aws_write_prefixes, std::move(p), segments, 2);

                state->binary_cache_providers.insert("aws");
            }
            else if (segments[0].second == "x-aws-config")
            {
                if (segments.size() != 2)
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresSingleStringArgument,
                                                 msg::binary_source = "x-aws-config"));
                }

                bool no_sign_request = false;
                if (segments[1].second == "no-sign-request")
                {
                    no_sign_request = true;
                }
                else
                {
                    return add_error(msg::format(msgInvalidArgument), segments[1].first);
                }

                state->aws_no_sign_request = no_sign_request;
                state->binary_cache_providers.insert("aws");
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

                handle_readwrite(state->cos_read_prefixes, state->cos_write_prefixes, std::move(p), segments, 2);
                state->binary_cache_providers.insert("cos");
            }
            else if (segments[0].second == "x-gha")
            {
                add_warning(msg::format(msgGhaBinaryCacheDeprecated, msg::url = docs::binarycaching_url));
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
                bool has_sha = false;
                bool has_other = false;
                api_stable_format(
                    null_diagnostic_context, url_template.url_template, [&](std::string&, StringView key) {
                        if (key == "sha")
                        {
                            has_sha = true;
                        }
                        else
                        {
                            has_other = true;
                        }

                        return true;
                    });
                if (!has_sha)
                {
                    if (has_other)
                    {
                        return add_error(msg::format(msgMissingShaVariable), segments[1].first);
                    }
                    if (url_template.url_template.back() != '/')
                    {
                        url_template.url_template.push_back('/');
                    }
                    url_template.url_template.append("{sha}.zip");
                }
                if (segments.size() == 4)
                {
                    url_template.headers.push_back(segments[3].second);
                }

                handle_readwrite(
                    state->url_templates_to_get, state->url_templates_to_put, std::move(url_template), segments, 2);
                state->binary_cache_providers.insert("http");
            }
            else if (segments[0].second == "x-az-universal")
            {
                // Scheme: x-az-universal,<organization>,<project>,<feed>[,<readwrite>]
                if (segments.size() < 4 || segments.size() > 5)
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresFourOrFiveArguments,
                                                 msg::binary_source = "Universal Packages"));
                }
                AzureUpkgSource upkg_template{
                    segments[1].second,
                    segments[2].second,
                    segments[3].second,
                };

                state->binary_cache_providers.insert("upkg");
                handle_readwrite(
                    state->upkg_templates_to_get, state->upkg_templates_to_put, std::move(upkg_template), segments, 4);
            }
            else if (segments[0].second == "x-azcopy")
            {
                handle_azcopy_segments(segments);
            }
            else if (segments[0].second == "x-azcopy-sas")
            {
                handle_azcopy_sas_segments(segments);
            }
            else
            {
                return add_error(msg::format(msgUnknownBinaryProviderType), segments[0].first);
            }
        }
    };

    struct AssetSourcesState
    {
        bool cleared = false;
        bool block_origin = false;
        std::vector<std::string> url_templates_to_get;
        std::vector<std::string> azblob_templates_to_put;
        std::vector<std::string> secrets;
        Optional<std::string> script;

        void clear()
        {
            cleared = true;
            block_origin = false;
            url_templates_to_get.clear();
            azblob_templates_to_put.clear();
            secrets.clear();
            script.clear();
        }
    };

    struct AssetSourcesParser : ConfigSegmentsParser
    {
        AssetSourcesParser(StringView text, StringView origin, AssetSourcesState* state)
            : ConfigSegmentsParser(text, origin, {0, 0}), state(state)
        {
        }

        AssetSourcesState* state;

        void parse()
        {
            auto all_segments = parse_all_segments();
            for (auto&& x : all_segments)
            {
                if (messages().any_errors()) return;
                handle_segments(std::move(x));
            }
        }

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
            else if (segments[0].second == "clear")
            {
                if (segments.size() >= 2)
                {
                    return add_error(msg::format(msgAssetCacheProviderAcceptsNoArguments, msg::value = "clear"),
                                     segments[1].first);
                }

                state->clear();
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
                handle_readwrite(
                    state->url_templates_to_get, state->azblob_templates_to_put, std::move(p), segments, 3);
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
                // Don't forget to update this message if new providers are added.
                return add_error(msg::format(msgUnexpectedAssetCacheProvider), segments[0].first);
            }
        }
    };
}

namespace vcpkg
{
    LocalizedString UrlTemplate::valid() const
    {
        BufferedDiagnosticContext bdc{out_sink};
        std::vector<std::string> invalid_keys;
        auto result = api_stable_format(bdc, url_template, [&](std::string&, StringView key) {
            static constexpr StringLiteral valid_keys[] = {"name", "version", "sha", "triplet"};
            if (!Util::Vectors::contains(valid_keys, key))
            {
                invalid_keys.push_back(key.to_string());
            }

            return true;
        });

        if (!invalid_keys.empty())
        {
            bdc.report_error(msg::format(msgUnknownVariablesInTemplate,
                                         msg::value = url_template,
                                         msg::list = Strings::join(", ", invalid_keys)));
            result.clear();
        }

        if (result.has_value())
        {
            return {};
        }

        return LocalizedString::from_raw(std::move(bdc).to_string());
    }

    std::string UrlTemplate::instantiate_variables(const BinaryPackageReadInfo& info) const
    {
        return api_stable_format(console_diagnostic_context,
                                 url_template,
                                 [&](std::string& out, StringView key) {
                                     if (key == "version")
                                     {
                                         out += info.version.text;
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
                                         Checks::unreachable(
                                             VCPKG_LINE_INFO,
                                             "used instantiate_variables without checking valid() first");
                                     };

                                     return true;
                                 })
            .value_or_exit(VCPKG_LINE_INFO);
    }

    std::string AzCopyUrl::make_object_path(const std::string& abi) const
    {
        const auto base_url = url.back() == '/' ? url : Strings::concat(url, "/");
        return sas.empty() ? Strings::concat(base_url, abi, ".zip") : Strings::concat(base_url, abi, ".zip?", sas);
    }

    std::string AzCopyUrl::make_container_path() const { return sas.empty() ? url : Strings::concat(url, "?", sas); }

    static NuGetRepoInfo get_nuget_repo_info_from_env(const VcpkgCmdArguments& args)
    {
        if (auto p = args.vcpkg_nuget_repository.get())
        {
            get_global_metrics_collector().track_define(DefineMetric::VcpkgNugetRepository);
            return {*p};
        }

        auto gh_repo = get_environment_variable(EnvironmentVariableGitHubRepository).value_or("");
        if (gh_repo.empty())
        {
            return {};
        }

        auto gh_server = get_environment_variable(EnvironmentVariableGitHubServerUrl).value_or("");
        if (gh_server.empty())
        {
            return {};
        }

        get_global_metrics_collector().track_define(DefineMetric::GitHubRepository);
        return {Strings::concat(gh_server, '/', gh_repo, ".git"),
                get_environment_variable(EnvironmentVariableGitHubRef).value_or(""),
                get_environment_variable(EnvironmentVariableGitHubSha).value_or("")};
    }

    void ReadOnlyBinaryCache::fetch(View<InstallPlanAction> actions)
    {
        std::vector<const InstallPlanAction*> action_ptrs;
        std::vector<RestoreResult> restores;
        std::vector<CacheStatus*> statuses;
        for (auto&& provider : m_config.read)
        {
            action_ptrs.clear();
            restores.clear();
            statuses.clear();
            for (size_t i = 0; i < actions.size(); ++i)
            {
                if (auto abi = actions[i].package_abi().get())
                {
                    CacheStatus& status = m_status[*abi];
                    if (status.should_attempt_restore(provider.get()))
                    {
                        action_ptrs.push_back(&actions[i]);
                        restores.push_back(RestoreResult::unavailable);
                        statuses.push_back(&status);
                    }
                }
            }
            if (action_ptrs.empty()) continue;

            ElapsedTimer timer;
            provider->fetch(action_ptrs, restores);
            size_t num_restored = 0;
            for (size_t i = 0; i < restores.size(); ++i)
            {
                if (restores[i] == RestoreResult::unavailable)
                {
                    statuses[i]->mark_unavailable(provider.get());
                }
                else
                {
                    statuses[i]->mark_restored();
                    ++num_restored;
                }
            }
            msg::println(provider->restored_message(
                num_restored, timer.elapsed().as<std::chrono::high_resolution_clock::duration>()));
        }
    }

    bool ReadOnlyBinaryCache::is_restored(const InstallPlanAction& action) const
    {
        if (auto abi = action.package_abi().get())
        {
            auto it = m_status.find(*abi);
            if (it != m_status.end()) return it->second.is_restored();
        }
        return false;
    }

    void ReadOnlyBinaryCache::install_read_provider(std::unique_ptr<IReadBinaryProvider>&& provider)
    {
        m_config.read.push_back(std::move(provider));
    }

    void ReadOnlyBinaryCache::mark_all_unrestored()
    {
        for (auto& entry : m_status)
        {
            entry.second.mark_unrestored();
        }
    }

    std::vector<CacheAvailability> ReadOnlyBinaryCache::precheck(View<const InstallPlanAction*> actions)
    {
        std::vector<CacheStatus*> statuses = Util::fmap(actions, [this](const auto& action) {
            Checks::check_exit(VCPKG_LINE_INFO, action && action->package_abi());
            ASSUME(action);
            return &m_status[*action->package_abi().get()];
        });

        std::vector<const InstallPlanAction*> action_ptrs;
        std::vector<CacheAvailability> cache_result;
        std::vector<size_t> indexes;
        for (auto&& provider : m_config.read)
        {
            action_ptrs.clear();
            cache_result.clear();
            indexes.clear();
            for (size_t i = 0; i < actions.size(); ++i)
            {
                if (statuses[i]->should_attempt_precheck(provider.get()))
                {
                    action_ptrs.push_back(actions[i]);
                    cache_result.push_back(CacheAvailability::unknown);
                    indexes.push_back(i);
                }
            }
            if (action_ptrs.empty()) continue;

            provider->precheck(action_ptrs, cache_result);

            for (size_t i = 0; i < action_ptrs.size(); ++i)
            {
                auto&& this_status = m_status[*action_ptrs[i]->package_abi().get()];
                if (cache_result[i] == CacheAvailability::available)
                {
                    this_status.mark_available(provider.get());
                }
                else if (cache_result[i] == CacheAvailability::unavailable)
                {
                    this_status.mark_unavailable(provider.get());
                }
            }
        }

        return Util::fmap(statuses, [](CacheStatus* s) {
            return s->get_available_provider() ? CacheAvailability::available : CacheAvailability::unavailable;
        });
    }

    void BinaryCacheSynchronizer::add_submitted() noexcept
    {
        // This can set the unused bit but if that happens we are terminating anyway.
        if ((m_state.fetch_add(1, std::memory_order_acq_rel) & SubmittedMask) == SubmittedMask)
        {
            Checks::unreachable(VCPKG_LINE_INFO, "Maximum job count exceeded");
        }
    }

    BinaryCacheSyncState BinaryCacheSynchronizer::fetch_add_completed() noexcept
    {
        auto old = m_state.load(std::memory_order_acquire);
        backing_uint_t local;
        do
        {
            local = old;
            if ((local & CompletedMask) == CompletedMask)
            {
                Checks::unreachable(VCPKG_LINE_INFO, "Maximum job count exceeded");
            }

            local += OneCompleted;
        } while (!m_state.compare_exchange_weak(old, local, std::memory_order_acq_rel));

        BinaryCacheSyncState result;
        result.jobs_submitted = local & SubmittedMask;
        result.jobs_completed = (local & CompletedMask) >> UpperShift;
        result.submission_complete = (local & SubmissionCompleteBit) != 0;
        return result;
    }

    BinaryCacheSynchronizer::counter_uint_t BinaryCacheSynchronizer::
        fetch_incomplete_mark_submission_complete() noexcept
    {
        auto old = m_state.load(std::memory_order_acquire);
        backing_uint_t local;
        BinaryCacheSynchronizer::counter_uint_t submitted;
        do
        {
            local = old;

            // Remove completions from the submission counter so that the (X/Y) console
            // output is prettier.
            submitted = local & SubmittedMask;
            auto completed = (local & CompletedMask) >> UpperShift;
            if (completed >= submitted)
            {
                local = SubmissionCompleteBit;
            }
            else
            {
                local = (submitted - completed) | SubmissionCompleteBit;
            }
        } while (!m_state.compare_exchange_weak(old, local, std::memory_order_acq_rel));
        auto state = m_state.fetch_or(SubmissionCompleteBit, std::memory_order_acq_rel);

        return (state & SubmittedMask) - ((state & CompletedMask) >> UpperShift);
    }

    bool BinaryCache::install_providers(const VcpkgCmdArguments& args,
                                        const VcpkgPaths& paths,
                                        MessageSink& status_sink)
    {
        if (args.binary_caching_enabled())
        {
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

            if (args.env_binary_sources.has_value())
            {
                get_global_metrics_collector().track_define(DefineMetric::VcpkgBinarySources);
            }

            if (args.cli_binary_sources.size() != 0)
            {
                get_global_metrics_collector().track_define(DefineMetric::BinaryCachingSource);
            }

            auto sRawHolder =
                parse_binary_provider_configs(args.env_binary_sources.value_or(""), args.cli_binary_sources);
            if (!sRawHolder)
            {
                status_sink.println(Color::error, std::move(sRawHolder).error());
                return false;
            }
            auto& s = *sRawHolder.get();

            static const std::map<StringLiteral, DefineMetric> metric_names{
                {"aws", DefineMetric::BinaryCachingAws},
                {"azblob", DefineMetric::BinaryCachingAzBlob},
                {"azcopy", DefineMetric::BinaryCachingAzCopy},
                {"azcopy-sas", DefineMetric::BinaryCachingAzCopySas},
                {"cos", DefineMetric::BinaryCachingCos},
                {"default", DefineMetric::BinaryCachingDefault},
                {"files", DefineMetric::BinaryCachingFiles},
                {"gcs", DefineMetric::BinaryCachingGcs},
                {"http", DefineMetric::BinaryCachingHttp},
                {"nuget", DefineMetric::BinaryCachingNuget},
                {"upkg", DefineMetric::BinaryCachingUpkg},
            };

            MetricsSubmission metrics;
            for (const auto& cache_provider : s.binary_cache_providers)
            {
                auto it = metric_names.find(cache_provider);
                if (it != metric_names.end())
                {
                    metrics.track_define(it->second);
                }
            }

            get_global_metrics_collector().track_submission(std::move(metrics));

            s.nuget_prefix = args.nuget_id_prefix.value_or("");
            if (!s.nuget_prefix.empty()) s.nuget_prefix.push_back('_');
            m_config.nuget_prefix = s.nuget_prefix;

            s.use_nuget_cache = args.use_nuget_cache.value_or(false);

            m_config.nuget_repo = get_nuget_repo_info_from_env(args);

            auto& fs = paths.get_filesystem();
            auto& tools = paths.get_tool_cache();
            const auto& buildtrees = paths.buildtrees();

            m_config.nuget_prefix = s.nuget_prefix;

            std::shared_ptr<const GcsStorageTool> gcs_tool;
            if (!s.gcs_read_prefixes.empty() || !s.gcs_write_prefixes.empty())
            {
                gcs_tool = std::make_shared<GcsStorageTool>(tools, out_sink);
            }
            std::shared_ptr<const AwsStorageTool> aws_tool;
            if (!s.aws_read_prefixes.empty() || !s.aws_write_prefixes.empty())
            {
                aws_tool = std::make_shared<AwsStorageTool>(tools, out_sink, s.aws_no_sign_request);
            }
            std::shared_ptr<const CosStorageTool> cos_tool;
            if (!s.cos_read_prefixes.empty() || !s.cos_write_prefixes.empty())
            {
                cos_tool = std::make_shared<CosStorageTool>(tools, out_sink);
            }
            Path azcopy_tool;
            if (!s.azcopy_read_templates.empty() || !s.azcopy_write_templates.empty())
            {
                azcopy_tool = tools.get_tool_path(Tools::AZCOPY, out_sink);
            }

            if (!s.archives_to_read.empty() || !s.url_templates_to_get.empty() || !s.gcs_read_prefixes.empty() ||
                !s.aws_read_prefixes.empty() || !s.cos_read_prefixes.empty() || !s.upkg_templates_to_get.empty() ||
                !s.azcopy_read_templates.empty())
            {
                ZipTool zip_tool;
                zip_tool.setup(tools, out_sink);
                for (auto&& dir : s.archives_to_read)
                {
                    m_config.read.push_back(std::make_unique<FilesReadBinaryProvider>(zip_tool, fs, std::move(dir)));
                }

                for (auto&& url : s.url_templates_to_get)
                {
                    m_config.read.push_back(
                        std::make_unique<HttpGetBinaryProvider>(zip_tool, fs, buildtrees, std::move(url), s.secrets));
                }

                for (auto&& prefix : s.gcs_read_prefixes)
                {
                    m_config.read.push_back(
                        std::make_unique<ObjectStorageProvider>(zip_tool, fs, buildtrees, std::move(prefix), gcs_tool));
                }

                for (auto&& prefix : s.aws_read_prefixes)
                {
                    m_config.read.push_back(
                        std::make_unique<ObjectStorageProvider>(zip_tool, fs, buildtrees, std::move(prefix), aws_tool));
                }

                for (auto&& prefix : s.cos_read_prefixes)
                {
                    m_config.read.push_back(
                        std::make_unique<ObjectStorageProvider>(zip_tool, fs, buildtrees, std::move(prefix), cos_tool));
                }

                for (auto&& src : s.upkg_templates_to_get)
                {
                    m_config.read.push_back(std::make_unique<AzureUpkgGetBinaryProvider>(
                        zip_tool, fs, tools, out_sink, std::move(src), buildtrees));
                }

                for (auto&& prefix : s.azcopy_read_templates)
                {
                    m_config.read.push_back(std::make_unique<AzCopyStorageProvider>(
                        zip_tool, fs, buildtrees, std::move(prefix), azcopy_tool));
                }
            }
            if (!s.upkg_templates_to_put.empty())
            {
                m_config.write.push_back(
                    std::make_unique<AzureUpkgPutBinaryProvider>(tools, out_sink, std::move(s.upkg_templates_to_put)));
            }
            if (!s.archives_to_write.empty())
            {
                m_config.write.push_back(
                    std::make_unique<FilesWriteBinaryProvider>(fs, std::move(s.archives_to_write)));
            }
            if (!s.azblob_templates_to_put.empty())
            {
                m_config.write.push_back(
                    std::make_unique<AzureBlobPutBinaryProvider>(fs, std::move(s.azblob_templates_to_put), s.secrets));
            }
            if (!s.url_templates_to_put.empty())
            {
                m_config.write.push_back(
                    std::make_unique<HTTPPutBinaryProvider>(std::move(s.url_templates_to_put), s.secrets));
            }
            if (!s.gcs_write_prefixes.empty())
            {
                m_config.write.push_back(
                    std::make_unique<ObjectStoragePushProvider>(std::move(s.gcs_write_prefixes), gcs_tool));
            }
            if (!s.aws_write_prefixes.empty())
            {
                m_config.write.push_back(
                    std::make_unique<ObjectStoragePushProvider>(std::move(s.aws_write_prefixes), aws_tool));
            }
            if (!s.cos_write_prefixes.empty())
            {
                m_config.write.push_back(
                    std::make_unique<ObjectStoragePushProvider>(std::move(s.cos_write_prefixes), cos_tool));
            }

            if (!s.sources_to_read.empty() || !s.configs_to_read.empty() || !s.sources_to_write.empty() ||
                !s.configs_to_write.empty())
            {
                NugetBaseBinaryProvider nuget_base(
                    fs, NuGetTool(tools, out_sink, s), paths.packages(), buildtrees, s.nuget_prefix);
                if (!s.sources_to_read.empty())
                    m_config.read.push_back(
                        std::make_unique<NugetReadBinaryProvider>(nuget_base, nuget_sources_arg(s.sources_to_read)));
                for (auto&& config : s.configs_to_read)
                    m_config.read.push_back(
                        std::make_unique<NugetReadBinaryProvider>(nuget_base, nuget_configfile_arg(config)));
                if (!s.sources_to_write.empty() || !s.configs_to_write.empty())
                {
                    m_config.write.push_back(std::make_unique<NugetBinaryPushProvider>(
                        nuget_base, std::move(s.sources_to_write), std::move(s.configs_to_write)));
                }
            }

            if (!s.azcopy_write_templates.empty())
            {
                m_config.write.push_back(
                    std::make_unique<AzCopyStoragePushProvider>(std::move(s.azcopy_write_templates), azcopy_tool));
            }
        }

        m_needs_nuspec_data = Util::any_of(m_config.write, [](auto&& p) { return p->needs_nuspec_data(); });
        m_needs_zip_file = Util::any_of(m_config.write, [](auto&& p) { return p->needs_zip_file(); });
        if (m_needs_zip_file)
        {
            m_zip_tool.setup(paths.get_tool_cache(), status_sink);
        }

        return true;
    }
    BinaryCache::BinaryCache(const Filesystem& fs)
        : m_fs(fs), m_bg_msg_sink(stdout_sink), m_push_thread(&BinaryCache::push_thread_main, this)
    {
    }
    BinaryCache::~BinaryCache() { wait_for_async_complete_and_join(); }

    void BinaryCache::push_success(CleanPackages clean_packages, const InstallPlanAction& action)
    {
        if (auto abi = action.package_abi().get())
        {
            bool restored;
            auto it = m_status.find(*abi);
            if (it == m_status.end())
            {
                restored = false;
            }
            else
            {
                restored = it->second.is_restored();

                // Purge all status information on push_success (cache invalidation)
                // - push_success may delete packages/ (invalidate restore)
                // - push_success may make the package available from providers (invalidate unavailable)
                m_status.erase(it);
            }

            if (!restored && !m_config.write.empty())
            {
                ElapsedTimer timer;
                BinaryPackageWriteInfo request{action};

                if (m_needs_nuspec_data)
                {
                    request.nuspec =
                        generate_nuspec(request.package_dir, action, m_config.nuget_prefix, m_config.nuget_repo);
                }

                if (m_config.write.size() == 1)
                {
                    request.unique_write_provider = true;
                }

                m_synchronizer.add_submitted();
                msg::println(msg::format(msgSubmittingBinaryCacheBackground,
                                         msg::spec = action.display_name(),
                                         msg::count = m_config.write.size()));
                m_actions_to_push.push(ActionToPush{std::move(request), clean_packages});
                return;
            }
        }

        if (clean_packages == CleanPackages::Yes)
        {
            m_fs.remove_all(action.package_dir.value_or_exit(VCPKG_LINE_INFO), VCPKG_LINE_INFO);
        }
    }

    void BinaryCache::print_updates() { m_bg_msg_sink.print_published(); }

    void BinaryCache::wait_for_async_complete_and_join()
    {
        m_bg_msg_sink.print_published();
        auto incomplete_count = m_synchronizer.fetch_incomplete_mark_submission_complete();
        if (incomplete_count != 0)
        {
            msg::println(msgWaitUntilPackagesUploaded, msg::count = incomplete_count);
        }

        m_bg_msg_sink.publish_directly_to_out_sink();
        m_actions_to_push.stop();
        if (m_push_thread.joinable())
        {
            m_push_thread.join();
        }
    }

    void BinaryCache::push_thread_main()
    {
        std::vector<ActionToPush> my_tasks;
        while (m_actions_to_push.get_work(my_tasks))
        {
            for (auto& action_to_push : my_tasks)
            {
                ElapsedTimer timer;
                if (m_needs_zip_file)
                {
                    Path zip_path = action_to_push.request.package_dir + ".zip";
                    PrintingDiagnosticContext pdc{m_bg_msg_sink};
                    if (m_zip_tool.compress_directory_to_zip(pdc, m_fs, action_to_push.request.package_dir, zip_path))
                    {
                        action_to_push.request.zip_path = std::move(zip_path);
                    }
                }

                size_t num_destinations = 0;
                for (auto&& provider : m_config.write)
                {
                    if (!provider->needs_zip_file() || action_to_push.request.zip_path.has_value())
                    {
                        num_destinations += provider->push_success(action_to_push.request, m_bg_msg_sink);
                    }
                }

                if (action_to_push.request.zip_path)
                {
                    m_fs.remove(*action_to_push.request.zip_path.get(), IgnoreErrors{});
                }

                if (action_to_push.clean_after_push == CleanPackages::Yes)
                {
                    m_fs.remove_all(action_to_push.request.package_dir, VCPKG_LINE_INFO);
                }

                auto sync_state = m_synchronizer.fetch_add_completed();
                auto message = msg::format(msgSubmittingBinaryCacheComplete,
                                           msg::spec = action_to_push.request.display_name,
                                           msg::count = num_destinations,
                                           msg::elapsed = timer.elapsed());
                if (sync_state.submission_complete)
                {
                    message.append_raw(fmt::format(" ({}/{})", sync_state.jobs_completed, sync_state.jobs_submitted));
                }

                m_bg_msg_sink.println(message);
            }
        }
    }

    bool CacheStatus::should_attempt_precheck(const IReadBinaryProvider* sender) const noexcept
    {
        switch (m_status)
        {
            case CacheStatusState::unknown: return !Util::Vectors::contains(m_known_unavailable_providers, sender);
            case CacheStatusState::available: return false;
            case CacheStatusState::restored: return false;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    bool CacheStatus::should_attempt_restore(const IReadBinaryProvider* sender) const noexcept
    {
        switch (m_status)
        {
            case CacheStatusState::unknown: return !Util::Vectors::contains(m_known_unavailable_providers, sender);
            case CacheStatusState::available: return m_available_provider == sender;
            case CacheStatusState::restored: return false;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    bool CacheStatus::is_unavailable(const IReadBinaryProvider* sender) const noexcept
    {
        return Util::Vectors::contains(m_known_unavailable_providers, sender);
    }

    bool CacheStatus::is_restored() const noexcept { return m_status == CacheStatusState::restored; }

    void CacheStatus::mark_unavailable(const IReadBinaryProvider* sender)
    {
        if (!Util::Vectors::contains(m_known_unavailable_providers, sender))
        {
            m_known_unavailable_providers.push_back(sender);
        }
    }
    void CacheStatus::mark_available(const IReadBinaryProvider* sender) noexcept
    {
        switch (m_status)
        {
            case CacheStatusState::unknown:
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
            case CacheStatusState::unknown: m_known_unavailable_providers.clear(); [[fallthrough]];
            case CacheStatusState::available: m_status = CacheStatusState::restored; break;
            case CacheStatusState::restored: break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    void CacheStatus::mark_unrestored() noexcept
    {
        if (m_status == CacheStatusState::restored)
        {
            m_status = CacheStatusState::available;
        }
    }

    const IReadBinaryProvider* CacheStatus::get_available_provider() const noexcept
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
        *this = BinaryConfigParserState();
        binary_cache_providers.insert("clear");
    }

    BinaryPackageReadInfo::BinaryPackageReadInfo(const InstallPlanAction& action)
        : package_abi(action.package_abi().value_or_exit(VCPKG_LINE_INFO))
        , spec(action.spec)
        , display_name(action.display_name())
        , version(action.version())
        , package_dir(action.package_dir.value_or_exit(VCPKG_LINE_INFO))
    {
    }
}

ExpectedL<AssetCachingSettings> vcpkg::parse_download_configuration(const Optional<std::string>& arg)
{
    AssetCachingSettings result;
    if (!arg || arg.get()->empty()) return result;

    get_global_metrics_collector().track_define(DefineMetric::AssetSource);

    AssetSourcesState s;
    const auto source = format_environment_variable(EnvironmentVariableXVcpkgAssetSources);
    AssetSourcesParser parser(*arg.get(), source, &s);
    parser.parse();
    if (parser.messages().any_errors())
    {
        auto&& messages = std::move(parser).extract_messages();
        messages.add_line(DiagnosticLine{DiagKind::Note, msg::format(msgSeeURL, msg::url = docs::assetcaching_url)});
        return messages.join();
    }

    if (s.azblob_templates_to_put.size() > 1)
    {
        return msg::format_error(msgAMaximumOfOneAssetWriteUrlCanBeSpecified)
            .append_raw('\n')
            .append_raw(NotePrefix)
            .append(msgSeeURL, msg::url = docs::assetcaching_url);
    }
    if (s.url_templates_to_get.size() > 1)
    {
        return msg::format_error(msgAMaximumOfOneAssetReadUrlCanBeSpecified)
            .append_raw('\n')
            .append_raw(NotePrefix)
            .append(msgSeeURL, msg::url = docs::assetcaching_url);
    }

    if (!s.url_templates_to_get.empty())
    {
        result.m_read_url_template = std::move(s.url_templates_to_get.back());
    }

    if (!s.azblob_templates_to_put.empty())
    {
        result.m_write_url_template = std::move(s.azblob_templates_to_put.back());
        auto v = azure_blob_headers();
        result.m_write_headers.assign(v.begin(), v.end());
    }

    result.m_secrets = std::move(s.secrets);
    result.m_block_origin = s.block_origin;
    result.m_script = std::move(s.script);
    return result;
}

ExpectedL<BinaryConfigParserState> vcpkg::parse_binary_provider_configs(const std::string& env_string,
                                                                        View<std::string> args)
{
    BinaryConfigParserState s;

    BinaryConfigParser default_parser("default,readwrite", "<defaults>", &s);
    default_parser.parse();
    if (default_parser.messages().any_errors())
    {
        return default_parser.messages().join();
    }

    for (const auto& line : default_parser.messages().lines())
    {
        line.print_to(out_sink);
    }

    // must live until the end of the function due to StringView in BinaryConfigParser
    const auto source = format_environment_variable("VCPKG_BINARY_SOURCES");
    BinaryConfigParser env_parser(env_string, source, &s);
    env_parser.parse();
    if (env_parser.messages().any_errors())
    {
        return env_parser.messages().join();
    }

    for (const auto& line : env_parser.messages().lines())
    {
        line.print_to(out_sink);
    }

    for (auto&& arg : args)
    {
        BinaryConfigParser arg_parser(arg, nullopt, &s);
        arg_parser.parse();
        if (arg_parser.messages().any_errors())
        {
            return arg_parser.messages().join();
        }

        for (const auto& line : arg_parser.messages().lines())
        {
            line.print_to(out_sink);
        }
    }

    return s;
}

std::string vcpkg::format_version_for_feedref(StringView version_text, StringView abi_tag)
{
    // this cannot use DotVersion::try_parse or DateVersion::try_parse,
    // since this is a subtly different algorithm
    // and ignores random extra stuff from the end

    ParsedExternalVersion parsed_version;
    if (try_extract_external_date_version(parsed_version, version_text))
    {
        parsed_version.normalize();
        return fmt::format(
            "{}.{}.{}-vcpkg{}", parsed_version.major, parsed_version.minor, parsed_version.patch, abi_tag);
    }

    if (!version_text.empty() && version_text[0] == 'v')
    {
        version_text = version_text.substr(1);
    }
    if (try_extract_external_dot_version(parsed_version, version_text))
    {
        parsed_version.normalize();
        return fmt::format(
            "{}.{}.{}-vcpkg{}", parsed_version.major, parsed_version.minor, parsed_version.patch, abi_tag);
    }

    return Strings::concat("0.0.0-vcpkg", abi_tag);
}

std::string vcpkg::generate_nuspec(const Path& package_dir,
                                   const InstallPlanAction& action,
                                   StringView id_prefix,
                                   const NuGetRepoInfo& rinfo)
{
    auto& spec = action.spec;
    auto& scf = *action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).source_control_file;
    auto& version = scf.core_paragraph->version;
    const auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);
    const auto& compiler_info = abi_info.compiler_info.value_or_exit(VCPKG_LINE_INFO);
    auto ref = make_nugetref(action, id_prefix);
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
    table.format("x-az-universal,<organization>,<project>,<feed>[,<rw>]", msg::format(msgHelpBinaryCachingAzUpkg));
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

std::string vcpkg::generate_nuget_packages_config(const ActionPlan& plan, StringView prefix)
{
    XmlSerializer xml;
    xml.emit_declaration().line_break();
    xml.open_tag("packages").line_break();
    for (auto&& action : plan.install_actions)
    {
        auto ref = make_nugetref(action, prefix);
        xml.start_complex_open_tag("package")
            .text_attr("id", ref.id)
            .text_attr("version", ref.version)
            .finish_self_closing_complex_tag()
            .line_break();
    }

    xml.close_tag("packages").line_break();
    return std::move(xml.buf);
}

FeedReference vcpkg::make_nugetref(const InstallPlanAction& action, StringView prefix)
{
    return ::make_feedref(
        action.spec, action.version(), action.abi_info.value_or_exit(VCPKG_LINE_INFO).package_abi, prefix);
}

std::vector<std::vector<std::string>> vcpkg::batch_command_arguments_with_fixed_length(
    const std::vector<std::string>& entries,
    const std::size_t reserved_len,
    const std::size_t max_len,
    const std::size_t fixed_len,
    const std::size_t separator_len)
{
    const auto available_len = static_cast<ptrdiff_t>(max_len) - reserved_len;

    // Not enough space for even one entry
    if (available_len < fixed_len) return {};

    const size_t entries_per_batch = 1 + (available_len - fixed_len) / (fixed_len + separator_len);

    auto first = entries.begin();
    const auto last = entries.end();
    std::vector<std::vector<std::string>> batches;
    while (first != last)
    {
        auto end_of_batch = first + std::min(static_cast<size_t>(last - first), entries_per_batch);
        batches.emplace_back(first, end_of_batch);
        first = end_of_batch;
    }
    return batches;
}
