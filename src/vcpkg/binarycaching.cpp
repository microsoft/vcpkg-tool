#include <vcpkg/base/api-stable-format.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/lazy.h>
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
#include <vcpkg/commands.build.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgpaths.h>

#include <iterator>
#include <numeric>

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

    NugetReference make_nugetref(const PackageSpec& spec, StringView raw_version, StringView abi_tag, StringView prefix)
    {
        return {Strings::concat(prefix, spec.dir()), format_version_for_nugetref(raw_version, abi_tag)};
    }
    NugetReference make_nugetref(const BinaryPackageReadInfo& info, StringView prefix)
    {
        return make_nugetref(info.spec, info.raw_version, info.package_abi, prefix);
    }

    void clean_prepare_dir(const Filesystem& fs, const Path& dir)
    {
        fs.remove_all(dir, VCPKG_LINE_INFO);
        if (!fs.create_directories(dir, VCPKG_LINE_INFO))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgUnableToClearPath, msg::path = dir);
        }
    }

    Path make_temp_archive_path(const Path& buildtrees, const PackageSpec& spec)
    {
        return buildtrees / spec.name() / (spec.triplet().to_string() + ".zip");
    }

    Path files_archive_subpath(const std::string& abi) { return Path(abi.substr(0, 2)) / (abi + ".zip"); }

    struct FilesWriteBinaryProvider : IWriteBinaryProvider
    {
        FilesWriteBinaryProvider(const Filesystem& fs, std::vector<Path>&& dirs) : m_fs(fs), m_dirs(std::move(dirs)) { }

        size_t push_success(const BinaryPackageWriteInfo& request, MessageSink& msg_sink) override
        {
            const auto& zip_path = request.zip_path.value_or_exit(VCPKG_LINE_INFO);
            const auto archive_subpath = files_archive_subpath(request.package_abi);

            size_t count_stored = 0;
            for (const auto& archives_root_dir : m_dirs)
            {
                const auto archive_path = archives_root_dir / archive_subpath;
                std::error_code ec;
                m_fs.create_directories(archive_path.parent_path(), IgnoreErrors{});
                m_fs.copy_file(zip_path, archive_path, CopyOptions::overwrite_existing, ec);
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
        on_fail,
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

            std::vector<Command> jobs;
            std::vector<size_t> action_idxs;
            for (size_t i = 0; i < actions.size(); ++i)
            {
                if (!zip_paths[i]) continue;
                const auto& pkg_path = actions[i]->package_dir.value_or_exit(VCPKG_LINE_INFO);
                clean_prepare_dir(m_fs, pkg_path);
                jobs.push_back(m_zip.decompress_zip_archive_cmd(pkg_path, zip_paths[i].get()->path));
                action_idxs.push_back(i);
            }

            auto job_results = decompress_in_parallel(jobs);

            for (size_t j = 0; j < jobs.size(); ++j)
            {
                const auto i = action_idxs[j];
                const auto& zip_path = zip_paths[i].value_or_exit(VCPKG_LINE_INFO);
                if (job_results[j])
                {
                    Debug::print("Restored ", zip_path.path, '\n');
                    out_status[i] = RestoreResult::restored;
                }
                else
                {
                    Debug::print("Failed to decompress archive package: ", zip_path.path, '\n');
                }

                post_decompress(zip_path, job_results[j].has_value());
            }
        }

        void post_decompress(const ZipResource& r, bool succeeded) const
        {
            if ((!succeeded && r.to_remove == RemoveWhen::on_fail) || r.to_remove == RemoveWhen::always)
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
                    auto to_remove = actions[i]->build_options.purge_decompress_failure == PurgeDecompressFailure::YES
                                         ? RemoveWhen::on_fail
                                         : RemoveWhen::nothing;
                    out_zip_paths[i].emplace(std::move(archive_path), to_remove);
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
        HTTPPutBinaryProvider(const Filesystem& fs,
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
            for (auto&& templ : m_urls)
            {
                auto url = templ.instantiate_variables(request);
                auto maybe_success = put_file(m_fs, url, m_secrets, templ.headers, zip_path);
                if (maybe_success)
                    count_stored++;
                else
                    msg_sink.println(Color::warning, maybe_success.error());
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
                url_paths.emplace_back(m_url_template.instantiate_variables(BinaryPackageReadInfo{action}),
                                       make_temp_archive_path(m_buildtrees, action.spec));
            }

            auto codes = download_files(m_fs, url_paths, m_url_template.headers);

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

            auto codes = url_heads(urls, {}, m_secrets);
            for (size_t i = 0; i < codes.size(); ++i)
            {
                out_status[i] = codes[i] == 200 ? CacheAvailability::available : CacheAvailability::unavailable;
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
            Command cmd = m_cmd;
            cmd.string_arg(sub).string_arg("-ForceEnglishOutput").string_arg("-Verbosity").string_arg("detailed");
            if (!m_interactive) cmd.string_arg("-NonInteractive");
            return cmd;
        }

        Command install_cmd(StringView packages_config, const Path& out_dir, const NuGetSource& src) const
        {
            Command cmd = subcommand("install");
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

        static std::string generate_packages_config(View<NugetReference> refs)
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
            m_cmd.install(stdout_sink, packages_config, m_packages, m_src);
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
            auto nupkg_path = m_buildtrees / make_nugetref(request, m_nuget_prefix).nupkg_filename();
            for (auto&& write_src : m_sources)
            {
                msg_sink.println(
                    msgUploadingBinariesToVendor, msg::spec = spec, msg::vendor = "NuGet", msg::path = write_src);
                if (!m_cmd.push(msg_sink, nupkg_path, nuget_sources_arg({&write_src, 1})))
                {
                    msg_sink.println(
                        Color::error, msgPushingVendorFailed, msg::vendor = "NuGet", msg::path = write_src);
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
                        Color::error, msgPushingVendorFailed, msg::vendor = "NuGet config", msg::path = write_cfg);
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

    struct GHABinaryProvider : ZipReadBinaryProvider
    {
        GHABinaryProvider(
            ZipTool zip, const Filesystem& fs, const Path& buildtrees, const std::string& url, const std::string& token)
            : ZipReadBinaryProvider(std::move(zip), fs)
            , m_buildtrees(buildtrees)
            , m_url(url + "_apis/artifactcache/cache")
            , m_token_header("Authorization: Bearer " + token)
        {
        }

        std::string lookup_cache_entry(StringView name, const std::string& abi) const
        {
            auto url = format_url_query(m_url, std::vector<std::string>{"keys=" + name + "-" + abi, "version=" + abi});
            auto res =
                invoke_http_request("GET",
                                    std::vector<std::string>{
                                        m_content_type_header.to_string(), m_token_header, m_accept_header.to_string()},
                                    url);
            if (auto p = res.get())
            {
                auto maybe_json = Json::parse_object(*p);
                if (auto json = maybe_json.get())
                {
                    auto archive_location = json->get("archiveLocation");
                    if (archive_location && archive_location->is_string())
                    {
                        return archive_location->string(VCPKG_LINE_INFO).to_string();
                    }
                }
            }
            return {};
        }

        void acquire_zips(View<const InstallPlanAction*> actions,
                          Span<Optional<ZipResource>> out_zip_paths) const override
        {
            std::vector<std::pair<std::string, Path>> url_paths;
            std::vector<size_t> url_indices;
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                auto&& action = *actions[idx];
                const auto& package_name = action.spec.name();
                auto url = lookup_cache_entry(package_name, action.package_abi().value_or_exit(VCPKG_LINE_INFO));
                if (url.empty()) continue;

                url_paths.emplace_back(std::move(url), make_temp_archive_path(m_buildtrees, action.spec));
                url_indices.push_back(idx);
            }

            auto codes = download_files(m_fs, url_paths, {});

            for (size_t i = 0; i < codes.size(); ++i)
            {
                if (codes[i] == 200)
                {
                    out_zip_paths[url_indices[i]].emplace(std::move(url_paths[i].second), RemoveWhen::always);
                }
            }
        }

        void precheck(View<const InstallPlanAction*>, Span<CacheAvailability>) const override { }

        LocalizedString restored_message(size_t count,
                                         std::chrono::high_resolution_clock::duration elapsed) const override
        {
            return msg::format(msgRestoredPackagesFromGHA, msg::count = count, msg::elapsed = ElapsedTime(elapsed));
        }

        Path m_buildtrees;
        std::string m_url;
        std::string m_token_header;
        static constexpr StringLiteral m_accept_header = "Accept: application/json;api-version=6.0-preview.1";
        static constexpr StringLiteral m_content_type_header = "Content-Type: application/json";
    };

    struct GHABinaryPushProvider : IWriteBinaryProvider
    {
        GHABinaryPushProvider(const Filesystem& fs, const std::string& url, const std::string& token)
            : m_fs(fs), m_url(url + "_apis/artifactcache/caches"), m_token_header("Authorization: Bearer " + token)
        {
        }

        Optional<int64_t> reserve_cache_entry(const std::string& name, const std::string& abi, int64_t cacheSize) const
        {
            Json::Object payload;
            payload.insert("key", name + "-" + abi);
            payload.insert("version", abi);
            payload.insert("cacheSize", Json::Value::integer(cacheSize));

            std::vector<std::string> headers;
            headers.emplace_back(m_accept_header.data(), m_accept_header.size());
            headers.emplace_back(m_content_type_header.data(), m_content_type_header.size());
            headers.emplace_back(m_token_header);

            auto res = invoke_http_request("POST", headers, m_url, stringify(payload));
            if (auto p = res.get())
            {
                auto maybe_json = Json::parse_object(*p);
                if (auto json = maybe_json.get())
                {
                    auto cache_id = json->get("cacheId");
                    if (cache_id && cache_id->is_integer())
                    {
                        return cache_id->integer(VCPKG_LINE_INFO);
                    }
                }
            }
            return {};
        }

        size_t push_success(const BinaryPackageWriteInfo& request, MessageSink&) override
        {
            if (!request.zip_path) return 0;

            const auto& zip_path = *request.zip_path.get();
            const ElapsedTimer timer;
            const auto& abi = request.package_abi;

            size_t upload_count = 0;
            auto cache_size = m_fs.file_size(zip_path, VCPKG_LINE_INFO);

            if (auto cacheId = reserve_cache_entry(request.spec.name(), abi, cache_size))
            {
                std::vector<std::string> custom_headers{
                    m_token_header,
                    m_accept_header.to_string(),
                    "Content-Type: application/octet-stream",
                    "Content-Range: bytes 0-" + std::to_string(cache_size) + "/*",
                };
                auto url = m_url + "/" + std::to_string(*cacheId.get());

                if (put_file(m_fs, url, {}, custom_headers, zip_path, "PATCH"))
                {
                    Json::Object commit;
                    commit.insert("size", std::to_string(cache_size));
                    std::vector<std::string> headers;
                    headers.emplace_back(m_accept_header.data(), m_accept_header.size());
                    headers.emplace_back(m_content_type_header.data(), m_content_type_header.size());
                    headers.emplace_back(m_token_header);
                    auto res = invoke_http_request("POST", headers, url, stringify(commit));
                    if (res)
                    {
                        ++upload_count;
                    }
                    else
                    {
                        msg::println(res.error());
                    }
                }
            }
            return upload_count;
        }

        bool needs_nuspec_data() const override { return false; }
        bool needs_zip_file() const override { return true; }

        const Filesystem& m_fs;
        std::string m_url;
        std::string m_token_header;
        static constexpr StringLiteral m_content_type_header = "Content-Type: application/json";
        static constexpr StringLiteral m_accept_header = "Accept: application/json;api-version=6.0-preview.1";
    };

    struct IObjectStorageTool
    {
        virtual ~IObjectStorageTool() = default;

        virtual LocalizedString restored_message(size_t count,
                                                 std::chrono::high_resolution_clock::duration elapsed) const = 0;
        virtual ExpectedL<Unit> stat(StringView url) const = 0;
        virtual ExpectedL<Unit> download_file(StringView object, const Path& archive) const = 0;
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
                auto tmp = make_temp_archive_path(m_buildtrees, action.spec);
                auto res = m_tool->download_file(make_object_path(m_prefix, abi), tmp);
                if (res)
                {
                    out_zip_paths[idx].emplace(std::move(tmp), RemoveWhen::always);
                }
                else
                {
                    stdout_sink.println_warning(res.error());
                }
            }
        }

        void precheck(View<const InstallPlanAction*> actions, Span<CacheAvailability> cache_status) const override
        {
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                auto&& action = *actions[idx];
                const auto& abi = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
                if (m_tool->stat(make_object_path(m_prefix, abi)))
                {
                    cache_status[idx] = CacheAvailability::available;
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
                    msg_sink.println_warning(res.error());
                }
            }
            return upload_count;
        }

        bool needs_nuspec_data() const override { return false; }
        bool needs_zip_file() const override { return true; }

        std::vector<std::string> m_prefixes;
        std::shared_ptr<const IObjectStorageTool> m_tool;
    };

    struct GcsStorageTool : IObjectStorageTool
    {
        GcsStorageTool(const ToolCache& cache, MessageSink& sink) : m_tool(cache.get_tool_path(Tools::GSUTIL, sink)) { }

        LocalizedString restored_message(size_t count,
                                         std::chrono::high_resolution_clock::duration elapsed) const override
        {
            return msg::format(msgRestoredPackagesFromGCS, msg::count = count, msg::elapsed = ElapsedTime(elapsed));
        }

        Command command() const { return m_tool; }

        ExpectedL<Unit> stat(StringView url) const override
        {
            auto cmd = command().string_arg("-q").string_arg("stat").string_arg(url);
            return flatten(cmd_execute_and_capture_output(cmd), Tools::GSUTIL);
        }

        ExpectedL<Unit> download_file(StringView object, const Path& archive) const override
        {
            auto cmd = command().string_arg("-q").string_arg("cp").string_arg(object).string_arg(archive);
            return flatten(cmd_execute_and_capture_output(cmd), Tools::GSUTIL);
        }

        ExpectedL<Unit> upload_file(StringView object, const Path& archive) const override
        {
            auto cmd = command().string_arg("-q").string_arg("cp").string_arg(archive).string_arg(object);
            return flatten(cmd_execute_and_capture_output(cmd), Tools::GSUTIL);
        }

        Command m_tool;
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

        Command command() const { return m_tool; }

        ExpectedL<Unit> stat(StringView url) const override
        {
            auto cmd = command().string_arg("s3").string_arg("ls").string_arg(url);
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }

            return flatten(cmd_execute_and_capture_output(cmd), Tools::AWSCLI);
        }

        ExpectedL<Unit> download_file(StringView object, const Path& archive) const override
        {
            auto r = stat(object);
            if (!r) return r;

            auto cmd = command().string_arg("s3").string_arg("cp").string_arg(object).string_arg(archive);
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }

            return flatten(cmd_execute_and_capture_output(cmd), Tools::AWSCLI);
        }

        ExpectedL<Unit> upload_file(StringView object, const Path& archive) const override
        {
            auto cmd = command().string_arg("s3").string_arg("cp").string_arg(archive).string_arg(object);
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }
            return flatten(cmd_execute_and_capture_output(cmd), Tools::AWSCLI);
        }

        Command m_tool;
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

        Command command() const { return m_tool; }

        ExpectedL<Unit> stat(StringView url) const override
        {
            auto cmd = command().string_arg("ls").string_arg(url);
            return flatten(cmd_execute_and_capture_output(cmd), Tools::COSCLI);
        }

        ExpectedL<Unit> download_file(StringView object, const Path& archive) const override
        {
            auto cmd = command().string_arg("cp").string_arg(object).string_arg(archive);
            return flatten(cmd_execute_and_capture_output(cmd), Tools::COSCLI);
        }

        ExpectedL<Unit> upload_file(StringView object, const Path& archive) const override
        {
            auto cmd = command().string_arg("cp").string_arg(archive).string_arg(object);
            return flatten(cmd_execute_and_capture_output(cmd), Tools::COSCLI);
        }

        Command m_tool;
    };

    ExpectedL<Path> default_cache_path_impl()
    {
        auto maybe_cachepath = get_environment_variable("VCPKG_DEFAULT_BINARY_CACHE");
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
        BinaryConfigParser(StringView text, StringView origin, BinaryConfigParserState* state)
            : ConfigSegmentsParser(text, origin), state(state)
        {
        }

        BinaryConfigParserState* state;

        void parse()
        {
            auto all_segments = parse_all_segments();
            for (auto&& x : all_segments)
            {
                if (get_error()) return;
                handle_segments(std::move(x));
            }
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
                bool read = false, write = false;
                handle_readwrite(read, write, segments, 3);
                if (read) state->url_templates_to_get.push_back(url_template);
                auto headers = azure_blob_headers();
                url_template.headers.assign(headers.begin(), headers.end());
                if (write) state->url_templates_to_put.push_back(url_template);

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
                // Scheme: x-gha[,<readwrite>]
                if (segments.size() > 2)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresZeroOrOneArgument, msg::binary_source = "gha"),
                        segments[2].first);
                }

                handle_readwrite(state->gha_read, state->gha_write, segments, 1);

                state->binary_cache_providers.insert("gha");
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
                    url_template.headers.push_back(segments[3].second);
                }

                handle_readwrite(
                    state->url_templates_to_get, state->url_templates_to_put, std::move(url_template), segments, 2);
                state->binary_cache_providers.insert("http");
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
            script = nullopt;
        }
    };

    struct AssetSourcesParser : ConfigSegmentsParser
    {
        AssetSourcesParser(StringView text, StringView origin, AssetSourcesState* state)
            : ConfigSegmentsParser(text, origin), state(state)
        {
        }

        AssetSourcesState* state;

        void parse()
        {
            auto all_segments = parse_all_segments();
            for (auto&& x : all_segments)
            {
                if (get_error()) return;
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
        std::vector<std::string> invalid_keys;
        auto result = api_stable_format(url_template, [&](std::string&, StringView key) {
            static constexpr std::array<StringLiteral, 4> valid_keys = {"name", "version", "sha", "triplet"};
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

    std::string UrlTemplate::instantiate_variables(const BinaryPackageReadInfo& info) const
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

    static NuGetRepoInfo get_nuget_repo_info_from_env(const VcpkgCmdArguments& args)
    {
        if (auto p = args.vcpkg_nuget_repository.get())
        {
            get_global_metrics_collector().track_define(DefineMetric::VcpkgNugetRepository);
            return {*p};
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

    static ExpectedL<BinaryProviders> make_binary_providers(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        BinaryProviders ret;
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
                return std::move(sRawHolder).error();
            }
            auto& s = *sRawHolder.get();

            static const std::map<StringLiteral, DefineMetric> metric_names{
                {"aws", DefineMetric::BinaryCachingAws},
                {"azblob", DefineMetric::BinaryCachingAzBlob},
                {"cos", DefineMetric::BinaryCachingCos},
                {"default", DefineMetric::BinaryCachingDefault},
                {"files", DefineMetric::BinaryCachingFiles},
                {"gcs", DefineMetric::BinaryCachingGcs},
                {"http", DefineMetric::BinaryCachingHttp},
                {"nuget", DefineMetric::BinaryCachingNuget},
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
            s.use_nuget_cache = args.use_nuget_cache.value_or(false);
            s.nuget_repo_info = get_nuget_repo_info_from_env(args);

            auto& fs = paths.get_filesystem();
            auto& tools = paths.get_tool_cache();
            const auto& buildtrees = paths.buildtrees();

            ret.nuget_prefix = s.nuget_prefix;

            std::shared_ptr<const GcsStorageTool> gcs_tool;
            if (!s.gcs_read_prefixes.empty() || !s.gcs_write_prefixes.empty())
            {
                gcs_tool = std::make_shared<GcsStorageTool>(tools, stdout_sink);
            }
            std::shared_ptr<const AwsStorageTool> aws_tool;
            if (!s.aws_read_prefixes.empty() || !s.aws_write_prefixes.empty())
            {
                aws_tool = std::make_shared<AwsStorageTool>(tools, stdout_sink, s.aws_no_sign_request);
            }
            std::shared_ptr<const CosStorageTool> cos_tool;
            if (!s.cos_read_prefixes.empty() || !s.cos_write_prefixes.empty())
            {
                cos_tool = std::make_shared<CosStorageTool>(tools, stdout_sink);
            }

            if (s.gha_read || s.gha_write)
            {
                if (!args.actions_cache_url.has_value() || !args.actions_runtime_token.has_value())
                    return msg::format_error(msgGHAParametersMissing,
                                             msg::url = "https://learn.microsoft.com/vcpkg/users/binarycaching#gha");
            }

            if (!s.archives_to_read.empty() || !s.url_templates_to_get.empty() || !s.gcs_read_prefixes.empty() ||
                !s.aws_read_prefixes.empty() || !s.cos_read_prefixes.empty() || s.gha_read)
            {
                auto maybe_zip_tool = ZipTool::make(tools, stdout_sink);
                if (!maybe_zip_tool.has_value())
                {
                    return std::move(maybe_zip_tool).error();
                }
                const auto& zip_tool = *maybe_zip_tool.get();

                for (auto&& dir : s.archives_to_read)
                {
                    ret.read.push_back(std::make_unique<FilesReadBinaryProvider>(zip_tool, fs, std::move(dir)));
                }

                for (auto&& url : s.url_templates_to_get)
                {
                    ret.read.push_back(
                        std::make_unique<HttpGetBinaryProvider>(zip_tool, fs, buildtrees, std::move(url), s.secrets));
                }

                for (auto&& prefix : s.gcs_read_prefixes)
                {
                    ret.read.push_back(
                        std::make_unique<ObjectStorageProvider>(zip_tool, fs, buildtrees, std::move(prefix), gcs_tool));
                }

                for (auto&& prefix : s.aws_read_prefixes)
                {
                    ret.read.push_back(
                        std::make_unique<ObjectStorageProvider>(zip_tool, fs, buildtrees, std::move(prefix), aws_tool));
                }

                for (auto&& prefix : s.cos_read_prefixes)
                {
                    ret.read.push_back(
                        std::make_unique<ObjectStorageProvider>(zip_tool, fs, buildtrees, std::move(prefix), cos_tool));
                }

                if (s.gha_read)
                {
                    const auto& url = *args.actions_cache_url.get();
                    const auto& token = *args.actions_runtime_token.get();
                    ret.read.push_back(std::make_unique<GHABinaryProvider>(zip_tool, fs, buildtrees, url, token));
                }
            }
            if (!s.archives_to_write.empty())
            {
                ret.write.push_back(std::make_unique<FilesWriteBinaryProvider>(fs, std::move(s.archives_to_write)));
            }
            if (!s.url_templates_to_put.empty())
            {
                ret.write.push_back(
                    std::make_unique<HTTPPutBinaryProvider>(fs, std::move(s.url_templates_to_put), s.secrets));
            }
            if (!s.gcs_write_prefixes.empty())
            {
                ret.write.push_back(
                    std::make_unique<ObjectStoragePushProvider>(std::move(s.gcs_write_prefixes), gcs_tool));
            }
            if (!s.aws_write_prefixes.empty())
            {
                ret.write.push_back(
                    std::make_unique<ObjectStoragePushProvider>(std::move(s.aws_write_prefixes), aws_tool));
            }
            if (!s.cos_write_prefixes.empty())
            {
                ret.write.push_back(
                    std::make_unique<ObjectStoragePushProvider>(std::move(s.cos_write_prefixes), cos_tool));
            }
            if (s.gha_write)
            {
                const auto& url = *args.actions_cache_url.get();
                const auto& token = *args.actions_runtime_token.get();
                ret.write.push_back(std::make_unique<GHABinaryPushProvider>(fs, url, token));
            }

            if (!s.sources_to_read.empty() || !s.configs_to_read.empty() || !s.sources_to_write.empty() ||
                !s.configs_to_write.empty())
            {
                NugetBaseBinaryProvider nuget_base(
                    fs, NuGetTool(tools, stdout_sink, s), paths.packages(), buildtrees, s.nuget_prefix);
                if (!s.sources_to_read.empty())
                    ret.read.push_back(
                        std::make_unique<NugetReadBinaryProvider>(nuget_base, nuget_sources_arg(s.sources_to_read)));
                for (auto&& config : s.configs_to_read)
                    ret.read.push_back(
                        std::make_unique<NugetReadBinaryProvider>(nuget_base, nuget_configfile_arg(config)));
                if (!s.sources_to_write.empty() || !s.configs_to_write.empty())
                {
                    ret.write.push_back(std::make_unique<NugetBinaryPushProvider>(
                        nuget_base, std::move(s.sources_to_write), std::move(s.configs_to_write)));
                }
            }
        }
        return std::move(ret);
    }

    ReadOnlyBinaryCache::ReadOnlyBinaryCache(BinaryProviders&& providers) : m_config(std::move(providers)) { }

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
                if (actions[i].package_abi())
                {
                    CacheStatus& status = m_status[*actions[i].package_abi().get()];
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

    std::vector<CacheAvailability> ReadOnlyBinaryCache::precheck(View<InstallPlanAction> actions)
    {
        std::vector<CacheStatus*> statuses = Util::fmap(actions, [this](const auto& action) {
            if (!action.package_abi()) Checks::unreachable(VCPKG_LINE_INFO);
            return &m_status[*action.package_abi().get()];
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
                    action_ptrs.push_back(&actions[i]);
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

    BinaryCache::BinaryCache(const Filesystem& fs) : m_fs(fs) { }

    ExpectedL<BinaryCache> BinaryCache::make(const VcpkgCmdArguments& args, const VcpkgPaths& paths, MessageSink& sink)
    {
        return make_binary_providers(args, paths).then([&](BinaryProviders&& p) -> ExpectedL<BinaryCache> {
            BinaryCache b(std::move(p), paths.get_filesystem());
            b.m_needs_nuspec_data = Util::any_of(b.m_config.write, [](auto&& p) { return p->needs_nuspec_data(); });
            b.m_needs_zip_file = Util::any_of(b.m_config.write, [](auto&& p) { return p->needs_zip_file(); });
            if (b.m_needs_zip_file)
            {
                auto maybe_zt = ZipTool::make(paths.get_tool_cache(), sink);
                if (auto z = maybe_zt.get())
                {
                    b.m_zip_tool.emplace(std::move(*z));
                }
                else
                {
                    return std::move(maybe_zt).error();
                }
            }
            return std::move(b);
        });
    }

    BinaryCache::BinaryCache(BinaryProviders&& providers, const Filesystem& fs)
        : ReadOnlyBinaryCache(std::move(providers)), m_fs(fs)
    {
    }
    BinaryCache::~BinaryCache() { }

    void BinaryCache::push_success(const InstallPlanAction& action)
    {
        if (auto abi = action.package_abi().get())
        {
            bool restored = m_status[*abi].is_restored();
            // Purge all status information on push_success (cache invalidation)
            // - push_success may delete packages/ (invalidate restore)
            // - push_success may make the package available from providers (invalidate unavailable)
            m_status.erase(*abi);
            if (!restored && !m_config.write.empty())
            {
                ElapsedTimer timer;
                BinaryPackageWriteInfo request{action};

                if (m_needs_nuspec_data)
                {
                    request.nuspec =
                        generate_nuspec(request.package_dir, action, m_config.nuget_prefix, m_config.nuget_repo);
                }
                if (m_needs_zip_file)
                {
                    Path zip_path = request.package_dir + ".zip";
                    auto compress_result = m_zip_tool.value_or_exit(VCPKG_LINE_INFO)
                                               .compress_directory_to_zip(m_fs, request.package_dir, zip_path);
                    if (compress_result)
                    {
                        request.zip_path = std::move(zip_path);
                    }
                    else
                    {
                        stdout_sink.println(
                            Color::warning,
                            msg::format_warning(msgCompressFolderFailed, msg::path = request.package_dir)
                                .append_raw(' ')
                                .append_raw(compress_result.error()));
                    }
                }

                size_t num_destinations = 0;
                for (auto&& provider : m_config.write)
                {
                    if (!provider->needs_zip_file() || request.zip_path.has_value())
                    {
                        num_destinations += provider->push_success(request, stdout_sink);
                    }
                }
                if (request.zip_path)
                {
                    m_fs.remove(*request.zip_path.get(), IgnoreErrors{});
                }
                stdout_sink.println(
                    msgStoredBinariesToDestinations, msg::count = num_destinations, msg::elapsed = timer.elapsed());
            }
        }
        if (action.build_options.clean_packages == CleanPackages::YES)
        {
            m_fs.remove_all(action.package_dir.value_or_exit(VCPKG_LINE_INFO), VCPKG_LINE_INFO);
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
        , raw_version(action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO)
                          .source_control_file->core_paragraph->raw_version)
        , package_dir(action.package_dir.value_or_exit(VCPKG_LINE_INFO))
    {
    }
}

ExpectedL<DownloadManagerConfig> vcpkg::parse_download_configuration(const Optional<std::string>& arg)
{
    if (!arg || arg.get()->empty()) return DownloadManagerConfig{};

    get_global_metrics_collector().track_define(DefineMetric::AssetSource);

    AssetSourcesState s;
    const auto source = Strings::concat("$", VcpkgCmdArguments::ASSET_SOURCES_ENV);
    AssetSourcesParser parser(*arg.get(), source, &s);
    parser.parse();
    if (auto err = parser.get_error())
    {
        return LocalizedString::from_raw(err->to_string()) // note that this already contains error:
            .append_raw('\n')
            .append_raw(NotePrefix)
            .append(msgSeeURL, msg::url = docs::assetcaching_url);
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

    Optional<std::string> get_url;
    if (!s.url_templates_to_get.empty())
    {
        get_url = std::move(s.url_templates_to_get.back());
    }
    Optional<std::string> put_url;
    std::vector<std::string> put_headers;
    if (!s.azblob_templates_to_put.empty())
    {
        put_url = std::move(s.azblob_templates_to_put.back());
        auto v = azure_blob_headers();
        put_headers.assign(v.begin(), v.end());
    }

    return DownloadManagerConfig{std::move(get_url),
                                 std::vector<std::string>{},
                                 std::move(put_url),
                                 std::move(put_headers),
                                 std::move(s.secrets),
                                 s.block_origin,
                                 s.script};
}

ExpectedL<BinaryConfigParserState> vcpkg::parse_binary_provider_configs(const std::string& env_string,
                                                                        View<std::string> args)
{
    BinaryConfigParserState s;

    BinaryConfigParser default_parser("default,readwrite", "<defaults>", &s);
    default_parser.parse();
    if (auto err = default_parser.get_error())
    {
        return LocalizedString::from_raw(err->message);
    }

    BinaryConfigParser env_parser(env_string, "VCPKG_BINARY_SOURCES", &s);
    env_parser.parse();
    if (auto err = env_parser.get_error())
    {
        return LocalizedString::from_raw(err->to_string());
    }

    for (auto&& arg : args)
    {
        BinaryConfigParser arg_parser(arg, "<command>", &s);
        arg_parser.parse();
        if (auto err = arg_parser.get_error())
        {
            return LocalizedString::from_raw(err->to_string());
        }
    }

    return s;
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

std::string vcpkg::generate_nuspec(const Path& package_dir,
                                   const InstallPlanAction& action,
                                   StringView id_prefix,
                                   const NuGetRepoInfo& rinfo)
{
    auto& spec = action.spec;
    auto& scf = *action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).source_control_file;
    auto& version = scf.core_paragraph->raw_version;
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

NugetReference vcpkg::make_nugetref(const InstallPlanAction& action, StringView prefix)
{
    return ::make_nugetref(action.spec,
                           action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO)
                               .source_control_file->core_paragraph->raw_version,
                           action.abi_info.value_or_exit(VCPKG_LINE_INFO).package_abi,
                           prefix);
}
