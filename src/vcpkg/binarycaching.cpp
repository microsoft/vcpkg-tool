#include <vcpkg/base/checks.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
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

#include "vcpkg/base/api_stable_format.h"

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
                return add_error("unexpected argument: expected 'read', readwrite', or 'write'",
                                 segments[segment_idx].first);
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
                        return add_error("unexpected eof: trailing unescaped backticks (`) are not allowed");
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
        bool created_last = fs.create_directories(dir, VCPKG_LINE_INFO);
        Checks::check_exit(VCPKG_LINE_INFO, created_last, "unable to clear path: %s", dir);
    }

    static Path make_temp_archive_path(const Path& buildtrees, const PackageSpec& spec)
    {
        return buildtrees / spec.name() / (spec.triplet().to_string() + ".zip");
    }

    struct ArchivesBinaryProvider : IBinaryProvider
    {
        ArchivesBinaryProvider(const VcpkgPaths& paths,
                               std::vector<Path>&& read_dirs,
                               std::vector<Path>&& write_dirs,
                               std::vector<UrlTemplate>&& put_url_templates,
                               const std::vector<std::string>& secrets)
            : paths(paths)
            , m_read_dirs(std::move(read_dirs))
            , m_write_dirs(std::move(write_dirs))
            , m_put_url_templates(std::move(put_url_templates))
            , m_secrets(secrets)
        {
        }

        static Path make_archive_subpath(const std::string& abi) { return Path(abi.substr(0, 2)) / (abi + ".zip"); }

        void prefetch(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const override
        {
            std::vector<size_t> to_try_restore_idxs;
            std::vector<const InstallPlanAction*> to_try_restore;

            for (const auto& archives_root_dir : m_read_dirs)
            {
                const ElapsedTimer timer;
                to_try_restore_idxs.clear();
                to_try_restore.clear();
                for (size_t idx = 0; idx < cache_status.size(); ++idx)
                {
                    auto idx_cache_status = cache_status[idx];
                    if (idx_cache_status && idx_cache_status->should_attempt_restore(this))
                    {
                        to_try_restore_idxs.push_back(idx);
                        to_try_restore.push_back(&actions[idx]);
                    }
                }

                auto results = try_restore_n(to_try_restore, archives_root_dir);
                int num_restored = 0;
                for (size_t n = 0; n < to_try_restore.size(); ++n)
                {
                    if (results[n] == RestoreResult::restored)
                    {
                        cache_status[to_try_restore_idxs[n]]->mark_restored();
                        ++num_restored;
                    }
                }
                msg::println(msgRestoredPackagesFromVendor,
                             msg::count = num_restored,
                             msg::elapsed = timer.elapsed(),
                             msg::value = archives_root_dir.native());
            }
        }

        std::vector<RestoreResult> try_restore_n(View<const InstallPlanAction*> actions,
                                                 const Path& archives_root_dir) const
        {
            auto& fs = paths.get_filesystem();
            std::vector<RestoreResult> results(actions.size(), RestoreResult::unavailable);
            std::vector<size_t> action_idxs;
            std::vector<Command> jobs;
            std::vector<Path> archive_paths;
            for (size_t i = 0; i < actions.size(); ++i)
            {
                const auto& action = *actions[i];
                const auto& spec = action.spec;
                const auto& abi_tag = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
                const auto archive_subpath = make_archive_subpath(abi_tag);
                auto archive_path = archives_root_dir / archive_subpath;
                if (fs.exists(archive_path, IgnoreErrors{}))
                {
                    auto pkg_path = paths.package_dir(spec);
                    clean_prepare_dir(fs, pkg_path);
                    jobs.push_back(
                        decompress_zip_archive_cmd(paths.get_tool_cache(), stdout_sink, pkg_path, archive_path));
                    action_idxs.push_back(i);
                    archive_paths.push_back(std::move(archive_path));
                }
            }

            auto job_results = decompress_in_parallel(jobs);

            for (size_t j = 0; j < jobs.size(); ++j)
            {
                const auto i = action_idxs[j];
                const auto& archive_result = job_results[j];
                if (archive_result)
                {
                    results[i] = RestoreResult::restored;
                    Debug::print("Restored ", archive_paths[j].native(), '\n');
                }
                else
                {
                    if (actions[i]->build_options.purge_decompress_failure == PurgeDecompressFailure::YES)
                    {
                        Debug::print(
                            "Failed to decompress archive package; purging: ", archive_paths[j].native(), '\n');
                        fs.remove(archive_paths[j], IgnoreErrors{});
                    }
                    else
                    {
                        Debug::print("Failed to decompress archive package: ", archive_paths[j].native(), '\n');
                    }
                }
            }
            return results;
        }

        RestoreResult try_restore(const InstallPlanAction& action) const override
        {
            // Note: this method is almost never called -- it will only be called if another provider promised to
            // restore a package but then failed at runtime
            auto p_action = &action;
            for (const auto& archives_root_dir : m_read_dirs)
            {
                if (try_restore_n({&p_action, 1}, archives_root_dir)[0] == RestoreResult::restored)
                {
                    msg::println(msgRestoredPackage, msg::path = archives_root_dir.native());
                    return RestoreResult::restored;
                }
            }
            return RestoreResult::unavailable;
        }

        void push_success(const InstallPlanAction& action) const override
        {
            if (m_write_dirs.empty() && m_put_url_templates.empty())
            {
                return;
            }

            const auto& abi_tag = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
            auto& spec = action.spec;
            auto& fs = paths.get_filesystem();
            const auto archive_subpath = make_archive_subpath(abi_tag);
            const auto tmp_archive_path = make_temp_archive_path(paths.buildtrees(), spec);
            auto compress_result = compress_directory_to_zip(
                fs, paths.get_tool_cache(), stdout_sink, paths.package_dir(spec), tmp_archive_path);
            if (!compress_result)
            {
                msg::println(Color::warning,
                             msg::format_warning(msgCompressFolderFailed, msg::path = paths.package_dir(spec))
                                 .append_raw(' ')
                                 .append_raw(compress_result.error()));
                return;
            }
            size_t http_remotes_pushed = 0;
            for (auto&& put_url_template : m_put_url_templates)
            {
                auto url = put_url_template.instantiate_variables(action);
                auto maybe_success = put_file(fs, url, m_secrets, put_url_template.headers_for_put, tmp_archive_path);
                if (maybe_success)
                {
                    http_remotes_pushed++;
                    continue;
                }

                msg::println(Color::warning, maybe_success.error());
            }

            if (!m_put_url_templates.empty())
            {
                msg::println(msgUploadedBinaries, msg::count = http_remotes_pushed, msg::vendor = "HTTP remotes");
            }

            for (const auto& archives_root_dir : m_write_dirs)
            {
                const auto archive_path = archives_root_dir / archive_subpath;
                fs.create_directories(archive_path.parent_path(), IgnoreErrors{});
                std::error_code ec;
                if (m_write_dirs.size() > 1)
                {
                    fs.copy_file(tmp_archive_path, archive_path, CopyOptions::overwrite_existing, ec);
                }
                else
                {
                    fs.rename_or_copy(tmp_archive_path, archive_path, ".tmp", ec);
                }

                if (ec)
                {
                    msg::println(Color::warning,
                                 msg::format(msgFailedToStoreBinaryCache, msg::path = archive_path)
                                     .append_raw('\n')
                                     .append_raw(ec.message()));
                }
                else
                {
                    msg::println(msgStoredBinaryCache, msg::path = archive_path);
                }
            }
            // In the case of 1 write dir, the file will be moved instead of copied
            if (m_write_dirs.size() != 1)
            {
                fs.remove(tmp_archive_path, IgnoreErrors{});
            }
        }

        void precheck(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const override
        {
            auto& fs = paths.get_filesystem();
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                const auto& action = actions[idx];
                const auto& abi_tag = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
                if (!cache_status[idx]->should_attempt_precheck(this))
                {
                    continue;
                }

                const auto archive_subpath = make_archive_subpath(abi_tag);
                bool any_available = false;
                for (auto&& archives_root_dir : m_read_dirs)
                {
                    if (fs.exists(archives_root_dir / archive_subpath, IgnoreErrors{}))
                    {
                        any_available = true;
                        break;
                    }
                }

                if (any_available)
                {
                    cache_status[idx]->mark_available(this);
                }
                else
                {
                    cache_status[idx]->mark_unavailable(this);
                }
            }
        }

    private:
        const VcpkgPaths& paths;
        std::vector<Path> m_read_dirs;
        std::vector<Path> m_write_dirs;
        std::vector<UrlTemplate> m_put_url_templates;
        std::vector<std::string> m_secrets;
    };
    struct HttpGetBinaryProvider : IBinaryProvider
    {
        HttpGetBinaryProvider(const VcpkgPaths& paths,
                              std::vector<UrlTemplate>&& url_templates,
                              const std::vector<std::string>& secrets)
            : paths(paths), m_url_templates(std::move(url_templates)), m_secrets(secrets)
        {
        }

        RestoreResult try_restore(const InstallPlanAction&) const override { return RestoreResult::unavailable; }

        void push_success(const InstallPlanAction&) const override { }

        void prefetch(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const override
        {
            const ElapsedTimer timer;
            auto& fs = paths.get_filesystem();
            size_t this_restore_count = 0;
            std::vector<std::pair<std::string, Path>> url_paths;
            std::vector<size_t> url_indices;
            for (auto&& url_template : m_url_templates)
            {
                url_paths.clear();
                url_indices.clear();
                for (size_t idx = 0; idx < cache_status.size(); ++idx)
                {
                    auto this_cache_status = cache_status[idx];
                    if (!this_cache_status || !this_cache_status->should_attempt_restore(this))
                    {
                        continue;
                    }

                    auto&& action = actions[idx];
                    clean_prepare_dir(fs, paths.package_dir(action.spec));
                    auto uri = url_template.instantiate_variables(action);
                    url_paths.emplace_back(std::move(uri), make_temp_archive_path(paths.buildtrees(), action.spec));
                    url_indices.push_back(idx);
                }

                if (url_paths.empty()) break;

                msg::println(msgAttemptingToFetchPackagesFromVendor,
                             msg::count = url_paths.size(),
                             msg::vendor = "HTTP servers");

                auto codes = download_files(fs, url_paths, url_template.headers_for_get);
                std::vector<size_t> action_idxs;
                std::vector<Command> jobs;
                for (size_t i = 0; i < codes.size(); ++i)
                {
                    if (codes[i] == 200)
                    {
                        action_idxs.push_back(i);
                        jobs.push_back(decompress_zip_archive_cmd(paths.get_tool_cache(),
                                                                  stdout_sink,
                                                                  paths.package_dir(actions[url_indices[i]].spec),
                                                                  url_paths[i].second));
                    }
                }
                auto job_results = decompress_in_parallel(jobs);
                for (size_t j = 0; j < jobs.size(); ++j)
                {
                    const auto i = action_idxs[j];
                    if (job_results[j])
                    {
                        ++this_restore_count;
                        fs.remove(url_paths[i].second, VCPKG_LINE_INFO);
                        cache_status[url_indices[i]]->mark_restored();
                    }
                    else
                    {
                        Debug::print("Failed to decompress ", url_paths[i].second, '\n');
                    }
                }
            }

            msg::println(msgRestoredPackagesFromVendor,
                         msg::count = this_restore_count,
                         msg::elapsed = timer.elapsed(),
                         msg::value = "HTTP servers");
        }

        void precheck(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const override
        {
            std::vector<CacheAvailability> actions_present{actions.size()};
            std::vector<std::string> urls;
            std::vector<size_t> url_indices;
            for (auto&& url_template : m_url_templates)
            {
                urls.clear();
                url_indices.clear();
                for (size_t idx = 0; idx < actions.size(); ++idx)
                {
                    if (!cache_status[idx]->should_attempt_precheck(this))
                    {
                        continue;
                    }

                    urls.push_back(url_template.instantiate_variables(actions[idx]));
                    url_indices.push_back(idx);
                }

                if (urls.empty())
                {
                    return;
                }

                auto codes = url_heads(urls, {}, m_secrets);
                Checks::check_exit(VCPKG_LINE_INFO, codes.size() == urls.size());
                for (size_t i = 0; i < codes.size(); ++i)
                {
                    if (codes[i] == 200)
                    {
                        cache_status[url_indices[i]]->mark_available(this);
                        actions_present[url_indices[i]] = CacheAvailability::available;
                    }
                }
            }

            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                if (actions_present[idx] == CacheAvailability::unavailable)
                {
                    cache_status[idx]->mark_unavailable(this);
                }
            }
        }

        const VcpkgPaths& paths;
        std::vector<UrlTemplate> m_url_templates;
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
                            bool interactive)
            : paths(paths)
            , m_read_sources(std::move(read_sources))
            , m_write_sources(std::move(write_sources))
            , m_read_configs(std::move(read_configs))
            , m_write_configs(std::move(write_configs))
            , m_timeout(std::move(timeout))
            , m_interactive(interactive)
            , m_use_nuget_cache(false)
        {
            const std::string use_nuget_cache = get_environment_variable("VCPKG_USE_NUGET_CACHE").value_or("");
            m_use_nuget_cache =
                Strings::case_insensitive_ascii_equals(use_nuget_cache, "true") || use_nuget_cache == "1";
        }

        ExpectedS<Unit> run_nuget_commandline(const Command& cmdline) const
        {
            if (m_interactive)
            {
                return cmd_execute(cmdline)
                    .map_error([](LocalizedString&& ls) { return ls.extract_data(); })
                    .then([](int exit_code) -> ExpectedS<Unit> {
                        if (exit_code == 0)
                        {
                            return {Unit{}};
                        }

                        return "NuGet command failed and output was not captured because --interactive was specified";
                    });
            }

            return cmd_execute_and_capture_output(cmdline)
                .map_error([](LocalizedString&& ls) { return ls.extract_data(); })
                .then([&](ExitCodeAndOutput&& res) -> ExpectedS<Unit> {
                    if (Debug::g_debugging)
                    {
                        msg::write_unlocalized_text_to_stdout(Color::error, res.output);
                    }

                    if (res.output.find("Authentication may require manual action.") != std::string::npos)
                    {
                        msg::println(Color::warning, msgAuthenticationMayRequireManualAction, msg::vendor = "Nuget");
                    }

                    if (res.exit_code == 0)
                    {
                        return {Unit{}};
                    }

                    if (res.output.find("Response status code does not indicate success: 401 (Unauthorized)") !=
                        std::string::npos)
                    {
                        msg::println(Color::warning,
                                     msgFailedVendorAuthentication,
                                     msg::vendor = "NuGet",
                                     msg::url = docs::binarycaching_url);
                    }
                    else if (res.output.find("for example \"-ApiKey AzureDevOps\"") != std::string::npos)
                    {
                        auto real_cmdline = cmdline;
                        real_cmdline.string_arg("-ApiKey").string_arg("AzureDevOps");
                        return cmd_execute_and_capture_output(real_cmdline)
                            .map_error([](LocalizedString&& ls) { return ls.extract_data(); })
                            .then([](ExitCodeAndOutput&& res) -> ExpectedS<Unit> {
                                if (Debug::g_debugging)
                                {
                                    msg::write_unlocalized_text_to_stdout(Color::error, res.output);
                                }

                                if (res.exit_code == 0)
                                {
                                    return {Unit{}};
                                }

                                return {std::move(res.output), expected_right_tag};
                            });
                    }

                    return {std::move(res.output), expected_right_tag};
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
                run_nuget_commandline(cmdline);
                Util::erase_remove_if(attempts, [&](const NuGetPrefetchAttempt& nuget_ref) -> bool {
                    // note that we would like the nupkg downloaded to buildtrees, but nuget.exe downloads it to the
                    // output directory
                    const auto nupkg_path =
                        paths.packages() / nuget_ref.reference.id / nuget_ref.reference.id + ".nupkg";
                    if (fs.exists(nupkg_path, IgnoreErrors{}))
                    {
                        fs.remove(nupkg_path, VCPKG_LINE_INFO);
                        Checks::check_exit(VCPKG_LINE_INFO,
                                           !fs.exists(nupkg_path, IgnoreErrors{}),
                                           "Unable to remove nupkg after restoring: %s",
                                           nupkg_path);
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

        void push_success(const InstallPlanAction& action) const override
        {
            if (m_write_sources.empty() && m_write_configs.empty())
            {
                return;
            }

            auto& spec = action.spec;

            NugetReference nuget_ref = make_nugetref(action, get_nuget_prefix());
            auto nuspec_path = paths.buildtrees() / spec.name() / (spec.triplet().to_string() + ".nuspec");
            auto& fs = paths.get_filesystem();
            fs.write_contents(
                nuspec_path, generate_nuspec(paths.package_dir(spec), action, nuget_ref), VCPKG_LINE_INFO);

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

            if (!run_nuget_commandline(cmdline))
            {
                msg::println(Color::error, msgPackingVendorFailed, msg::vendor = "NuGet");
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
                msg::println(
                    msgUploadingBinariesToVendor, msg::spec = spec, msg::vendor = "NuGet", msg::path = write_src);
                if (!run_nuget_commandline(cmd))
                {
                    msg::println(Color::error, msgPushingVendorFailed, msg::vendor = "NuGet", msg::path = write_src);
                }
            }
            for (auto&& write_cfg : m_write_configs)
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
                    .string_arg("-ConfigFile")
                    .string_arg(write_cfg);
                if (!m_interactive)
                {
                    cmd.string_arg("-NonInteractive");
                }
                msg::println(Color::error,
                             msgUploadingBinariesUsingVendor,
                             msg::spec = spec,
                             msg::vendor = "NuGet config",
                             msg::path = write_cfg);
                if (!run_nuget_commandline(cmd))
                {
                    msg::println(Color::error, msgPushingVendorFailed, msg::vendor = "NuGet", msg::path = write_cfg);
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
    struct GHABinaryProvider : IBinaryProvider
    {
        GHABinaryProvider(const VcpkgPaths& paths,
                          std::vector<std::string>&& read_prefixes,
                          std::vector<std::string>&& write_prefixes)
            : paths(paths), m_read_prefixes(std::move(read_prefixes)), m_write_prefixes(std::move(write_prefixes))
        {
            m_read_cache_key = get_environment_variable("VCPKG_GHA_READ_KEY").value_or("vcpkg");
            m_write_cache_key = get_environment_variable("VCPKG_GHA_WRITE_KEY").value_or("vcpkg");

            auto token = get_environment_variable("VCPKG_ACTIONS_RUNTIME_TOKEN")
                             .value_or(get_environment_variable("ACTIONS_RUNTIME_TOKEN").value_or(""));
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

        std::string lookup_cache_entry(const std::string& prefix, const std::string& abi) const
        {
            auto url = build_read_url(prefix);
            auto cmd = command()
                           .string_arg(url)
                           .string_arg("-G")
                           .string_arg("-d")
                           .string_arg("keys=" + m_read_cache_key)
                           .string_arg("-d")
                           .string_arg("version=" + abi);

            std::vector<std::string> lines;
            auto res = cmd_execute_and_capture_output(cmd);
            if (!res.has_value() || res.get()->exit_code) return {};
            auto json = Json::parse_object(res.get()->output);
            if (!json.has_value() || !json.get()->contains("archiveLocation")) return {};
            return json.get()->get("archiveLocation")->string(VCPKG_LINE_INFO).to_string();
        }

        Optional<int64_t> reserve_cache_entry(const std::string& prefix,
                                              const std::string& abi,
                                              int64_t cacheSize) const
        {
            Json::Object payload;
            payload.insert("key", m_write_cache_key);
            payload.insert("version", abi);
            payload.insert("cacheSize", Json::Value::integer(cacheSize));
            auto url = build_write_url(prefix);
            auto cmd = command().string_arg(url).string_arg("-d").string_arg(stringify(payload));

            auto res = cmd_execute_and_capture_output(cmd);
            if (!res.has_value() || res.get()->exit_code) return {};
            auto json = Json::parse_object(res.get()->output);
            if (!json.has_value() || !json.get()->contains("cacheId")) return {};
            return json.get()->get("cacheId")->integer(VCPKG_LINE_INFO);
        }

        std::string build_read_url(std::string prefix) const { return std::move(prefix) + "_apis/artifactcache/cache"; }
        std::string build_write_url(std::string prefix) const
        {
            return std::move(prefix) + "_apis/artifactcache/caches";
        }

        void prefetch(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const override
        {
            auto& fs = paths.get_filesystem();

            const ElapsedTimer timer;
            size_t restored_count = 0;
            for (const auto& prefix : m_read_prefixes)
            {
                std::vector<std::pair<std::string, Path>> url_paths;
                std::vector<size_t> url_indices;

                for (size_t idx = 0; idx < cache_status.size(); ++idx)
                {
                    const auto this_cache_status = cache_status[idx];
                    if (!this_cache_status || !this_cache_status->should_attempt_restore(this))
                    {
                        continue;
                    }

                    auto&& action = actions[idx];
                    auto url = lookup_cache_entry(prefix, action.package_abi().value_or_exit(VCPKG_LINE_INFO));
                    if (url.empty()) continue;

                    clean_prepare_dir(fs, paths.package_dir(action.spec));
                    url_paths.emplace_back(std::move(url), make_temp_archive_path(paths.buildtrees(), action.spec));
                    url_indices.push_back(idx);
                }

                if (url_paths.empty()) break;

                msg::println(
                    msgAttemptingToFetchPackagesFromVendor, msg::count = url_paths.size(), msg::vendor = "GHA");

                auto codes = download_files(fs, url_paths, {});
                std::vector<size_t> action_idxs;
                std::vector<Command> jobs;
                for (size_t i = 0; i < codes.size(); ++i)
                {
                    if (codes[i] == 200)
                    {
                        action_idxs.push_back(i);
                        jobs.push_back(decompress_zip_archive_cmd(paths.get_tool_cache(),
                                                                  stdout_sink,
                                                                  paths.package_dir(actions[url_indices[i]].spec),
                                                                  url_paths[i].second));
                    }
                }
                auto job_results = decompress_in_parallel(jobs);
                for (size_t j = 0; j < jobs.size(); ++j)
                {
                    const auto i = action_idxs[j];
                    if (job_results[j])
                    {
                        ++restored_count;
                        fs.remove(url_paths[i].second, VCPKG_LINE_INFO);
                        cache_status[url_indices[i]]->mark_restored();
                    }
                    else
                    {
                        Debug::print("Failed to decompress ", url_paths[i].second, '\n');
                    }
                }
            }

            msg::println(msgRestoredPackagesFromVendor,
                         msg::count = restored_count,
                         msg::elapsed = timer.elapsed(),
                         msg::value = "GHA");
        }

        RestoreResult try_restore(const InstallPlanAction&) const override { return RestoreResult::unavailable; }

        void push_success(const InstallPlanAction& action) const override
        {
            if (m_write_prefixes.empty()) return;
            const ElapsedTimer timer;
            auto& fs = paths.get_filesystem();
            const auto& abi = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
            auto& spec = action.spec;
            const auto tmp_archive_path = make_temp_archive_path(paths.buildtrees(), spec);
            auto compression_result = compress_directory_to_zip(
                paths.get_filesystem(), paths.get_tool_cache(), stdout_sink, paths.package_dir(spec), tmp_archive_path);
            if (!compression_result)
            {
                vcpkg::msg::println(Color::warning,
                                    msg::format_warning(msgCompressFolderFailed, msg::path = paths.package_dir(spec))
                                        .append_raw(' ')
                                        .append_raw(compression_result.error()));
                return;
            }

            int64_t cache_size;
            {
                auto archive = fs.open_for_read(tmp_archive_path, VCPKG_LINE_INFO);
                archive.seek(0, SEEK_END);
                cache_size = archive.tell();
            }

            size_t upload_count = 0;
            for (const auto& prefix : m_write_prefixes)
            {
                auto cacheId = reserve_cache_entry(prefix, abi, cache_size);
                if (!cacheId) continue;

                std::vector<std::string> headers{
                    m_token_header,
                    m_accept_header.to_string(),
                    "Content-Type: application/octet-stream",
                    "Content-Range: bytes 0-" + std::to_string(cache_size) + "/*",
                };
                auto url = build_write_url(prefix) + "/" + std::to_string(*cacheId.get());
                if (!put_file(fs, url, {}, headers, tmp_archive_path, "PATCH"))
                {
                    continue;
                }

                Json::Object commit;
                commit.insert("size", std::to_string(cache_size));
                auto cmd = command().string_arg(url).string_arg("-d").string_arg(stringify(commit));

                auto res = cmd_execute_and_capture_output(cmd);
                if (!res.has_value() || res.get()->exit_code) continue;
                ++upload_count;
            }

            msg::println(msgUploadedPackagesToVendor,
                         msg::count = upload_count,
                         msg::elapsed = timer.elapsed(),
                         msg::vendor = "GHA");
        }

        void precheck(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const override
        {
            std::vector<CacheAvailability> actions_availability{actions.size()};
            for (const auto& prefix : m_read_prefixes)
            {
                for (size_t idx = 0; idx < actions.size(); ++idx)
                {
                    auto&& action = actions[idx];
                    const auto& abi = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
                    if (!cache_status[idx]->should_attempt_precheck(this))
                    {
                        continue;
                    }

                    if (!lookup_cache_entry(prefix, abi).empty())
                    {
                        actions_availability[idx] = CacheAvailability::available;
                        cache_status[idx]->mark_available(this);
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

        static constexpr StringLiteral m_accept_header = "Accept: application/json;api-version=6.0-preview.1";
        std::string m_token_header;

        std::string m_read_cache_key;
        std::string m_write_cache_key;

        const VcpkgPaths& paths;
        std::vector<std::string> m_read_prefixes;
        std::vector<std::string> m_write_prefixes;
    };

    struct ObjectStorageProvider : IBinaryProvider
    {
        ObjectStorageProvider(const VcpkgPaths& paths,
                              std::vector<std::string>&& read_prefixes,
                              std::vector<std::string>&& write_prefixes)
            : paths(paths), m_read_prefixes(std::move(read_prefixes)), m_write_prefixes(std::move(write_prefixes))
        {
        }

        static std::string make_object_path(const std::string& prefix, const std::string& abi)
        {
            return Strings::concat(prefix, abi, ".zip");
        }

        void prefetch(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const override
        {
            auto& fs = paths.get_filesystem();

            const ElapsedTimer timer;
            size_t restored_count = 0;
            for (const auto& prefix : m_read_prefixes)
            {
                std::vector<std::pair<std::string, Path>> url_paths;
                std::vector<size_t> url_indices;

                for (size_t idx = 0; idx < cache_status.size(); ++idx)
                {
                    const auto this_cache_status = cache_status[idx];
                    if (!this_cache_status || !this_cache_status->should_attempt_restore(this))
                    {
                        continue;
                    }

                    auto&& action = actions[idx];
                    clean_prepare_dir(fs, paths.package_dir(action.spec));
                    url_paths.emplace_back(
                        make_object_path(prefix, action.package_abi().value_or_exit(VCPKG_LINE_INFO)),
                        make_temp_archive_path(paths.buildtrees(), action.spec));
                    url_indices.push_back(idx);
                }

                if (url_paths.empty()) break;

                msg::println(
                    msgAttemptingToFetchPackagesFromVendor, msg::count = url_paths.size(), msg::vendor = vendor());

                std::vector<Command> jobs;
                std::vector<size_t> idxs;
                for (size_t idx = 0; idx < url_paths.size(); ++idx)
                {
                    auto&& action = actions[url_indices[idx]];
                    auto&& url_path = url_paths[idx];
                    if (!download_file(url_path.first, url_path.second)) continue;
                    jobs.push_back(decompress_zip_archive_cmd(
                        paths.get_tool_cache(), stdout_sink, paths.package_dir(action.spec), url_path.second));
                    idxs.push_back(idx);
                }

                const auto job_results =
                    cmd_execute_and_capture_output_parallel(jobs, default_working_directory, get_clean_environment());

                for (size_t j = 0; j < jobs.size(); ++j)
                {
                    const auto idx = idxs[j];
                    if (!job_results[j])
                    {
                        Debug::print("Failed to decompress ", url_paths[idx].second, '\n');
                        continue;
                    }

                    // decompression success
                    ++restored_count;
                    fs.remove(url_paths[idx].second, VCPKG_LINE_INFO);
                    cache_status[url_indices[idx]]->mark_restored();
                }
            }

            msg::println(msgRestoredPackagesFromVendor,
                         msg::count = restored_count,
                         msg::elapsed = timer.elapsed(),
                         msg::value = vendor());
        }

        RestoreResult try_restore(const InstallPlanAction&) const override { return RestoreResult::unavailable; }

        void push_success(const InstallPlanAction& action) const override
        {
            if (m_write_prefixes.empty()) return;
            const ElapsedTimer timer;
            const auto& abi = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
            auto& spec = action.spec;
            const auto tmp_archive_path = make_temp_archive_path(paths.buildtrees(), spec);
            auto compression_result = compress_directory_to_zip(
                paths.get_filesystem(), paths.get_tool_cache(), stdout_sink, paths.package_dir(spec), tmp_archive_path);
            if (!compression_result)
            {
                vcpkg::msg::println(Color::warning,
                                    msg::format_warning(msgCompressFolderFailed, msg::path = paths.package_dir(spec))
                                        .append_raw(' ')
                                        .append_raw(compression_result.error()));
                return;
            }

            size_t upload_count = 0;
            for (const auto& prefix : m_write_prefixes)
            {
                if (upload_file(make_object_path(prefix, abi), tmp_archive_path))
                {
                    ++upload_count;
                }
            }

            msg::println(msgUploadedPackagesToVendor,
                         msg::count = upload_count,
                         msg::elapsed = timer.elapsed(),
                         msg::vendor = vendor());
        }

        void precheck(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const override
        {
            std::vector<CacheAvailability> actions_availability{actions.size()};
            for (const auto& prefix : m_read_prefixes)
            {
                for (size_t idx = 0; idx < actions.size(); ++idx)
                {
                    auto&& action = actions[idx];
                    const auto& abi = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
                    if (!cache_status[idx]->should_attempt_precheck(this))
                    {
                        continue;
                    }

                    if (stat(make_object_path(prefix, abi)))
                    {
                        actions_availability[idx] = CacheAvailability::available;
                        cache_status[idx]->mark_available(this);
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

    protected:
        virtual StringLiteral vendor() const = 0;
        virtual bool stat(StringView url) const = 0;
        virtual bool upload_file(StringView object, const Path& archive) const = 0;
        virtual bool download_file(StringView object, const Path& archive) const = 0;

        const VcpkgPaths& paths;

    private:
        std::vector<std::string> m_read_prefixes;
        std::vector<std::string> m_write_prefixes;
    };

    struct GcsBinaryProvider : ObjectStorageProvider
    {
        GcsBinaryProvider(const VcpkgPaths& paths,
                          std::vector<std::string>&& read_prefixes,
                          std::vector<std::string>&& write_prefixes)
            : ObjectStorageProvider(paths, std::move(read_prefixes), std::move(write_prefixes))
        {
        }

        StringLiteral vendor() const override { return "GCS"; }

        Command command() const { return Command{paths.get_tool_exe(Tools::GSUTIL, stdout_sink)}; }

        bool stat(StringView url) const override
        {
            auto cmd = command().string_arg("-q").string_arg("stat").string_arg(url);
            return succeeded(cmd_execute(cmd));
        }

        bool upload_file(StringView object, const Path& archive) const override
        {
            auto cmd = command().string_arg("-q").string_arg("cp").string_arg(archive).string_arg(object);
            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::GSUTIL);
            if (out)
            {
                return true;
            }

            msg::write_unlocalized_text_to_stdout(Color::warning, out.error());
            return false;
        }

        bool download_file(StringView object, const Path& archive) const override
        {
            auto cmd = command().string_arg("-q").string_arg("cp").string_arg(object).string_arg(archive);
            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::GSUTIL);
            if (out)
            {
                return true;
            }

            msg::write_unlocalized_text_to_stdout(Color::warning, out.error());
            return false;
        }
    };

    struct AwsBinaryProvider : ObjectStorageProvider
    {
        AwsBinaryProvider(const VcpkgPaths& paths,
                          std::vector<std::string>&& read_prefixes,
                          std::vector<std::string>&& write_prefixes,
                          const bool no_sign_request)
            : ObjectStorageProvider(paths, std::move(read_prefixes), std::move(write_prefixes))
            , m_no_sign_request(no_sign_request)
        {
        }

        StringLiteral vendor() const override { return "AWS"; }

        Command command() const { return Command{paths.get_tool_exe(Tools::AWSCLI, stdout_sink)}; }

        bool stat(StringView url) const override
        {
            auto cmd = command().string_arg("s3").string_arg("ls").string_arg(url);
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }

            return succeeded(cmd_execute(cmd));
        }

        bool upload_file(StringView object, const Path& archive) const override
        {
            auto cmd = command().string_arg("s3").string_arg("cp").string_arg(archive).string_arg(object);
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }
            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::AWSCLI);
            if (out)
            {
                return true;
            }

            msg::write_unlocalized_text_to_stdout(Color::warning, out.error());
            return false;
        }

        bool download_file(StringView object, const Path& archive) const override
        {
            if (!stat(object))
            {
                return false;
            }

            auto cmd = command().string_arg("s3").string_arg("cp").string_arg(object).string_arg(archive);
            if (m_no_sign_request)
            {
                cmd.string_arg("--no-sign-request");
            }

            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::AWSCLI);
            if (out)
            {
                return true;
            }

            msg::write_unlocalized_text_to_stdout(Color::warning, out.error());
            return false;
        }

    private:
        bool m_no_sign_request;
    };

    struct CosBinaryProvider : ObjectStorageProvider
    {
        CosBinaryProvider(const VcpkgPaths& paths,
                          std::vector<std::string>&& read_prefixes,
                          std::vector<std::string>&& write_prefixes)
            : ObjectStorageProvider(paths, std::move(read_prefixes), std::move(write_prefixes))
        {
        }

        StringLiteral vendor() const override { return "COS"; }

        Command command() const { return Command{paths.get_tool_exe(Tools::COSCLI, stdout_sink)}; }

        bool stat(StringView url) const override
        {
            auto cmd = command().string_arg("ls").string_arg(url);
            return succeeded(cmd_execute(cmd));
        }

        bool upload_file(StringView object, const Path& archive) const override
        {
            auto cmd = command().string_arg("cp").string_arg(archive).string_arg(object);
            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::COSCLI);
            if (out)
            {
                return true;
            }

            msg::write_unlocalized_text_to_stdout(Color::warning, out.error());
            return false;
        }

        bool download_file(StringView object, const Path& archive) const override
        {
            auto cmd = command().string_arg("cp").string_arg(object).string_arg(archive);
            const auto out = flatten(cmd_execute_and_capture_output(cmd), Tools::COSCLI);
            if (out)
            {
                return true;
            }

            msg::write_unlocalized_text_to_stdout(Color::warning, out.error());
            return false;
        }
    };
}

namespace vcpkg
{
    LocalizedString UrlTemplate::valid() const
    {
        std::vector<std::string> invalid_keys;
        auto result = api_stable_format(url_template, [&](std::string&, StringView key) {
            StringView valid_keys[] = {"name", "version", "sha", "triplet"};
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

    std::string UrlTemplate::instantiate_variables(const InstallPlanAction& action) const
    {
        return api_stable_format(url_template,
                                 [&](std::string& out, StringView key) {
                                     if (key == "version")
                                     {
                                         out += action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO)
                                                    .source_control_file->core_paragraph->raw_version;
                                     }
                                     else if (key == "name")
                                     {
                                         out += action.spec.name();
                                     }
                                     else if (key == "triplet")
                                     {
                                         out += action.spec.triplet().canonical_name();
                                     }
                                     else if (key == "sha")
                                     {
                                         out += action.abi_info.value_or_exit(VCPKG_LINE_INFO).package_abi;
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

    BinaryCache::BinaryCache(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        install_providers_for(args, paths);
    }

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

    void BinaryCache::push_success(const InstallPlanAction& action)
    {
        const auto abi = action.package_abi().get();
        if (abi)
        {
            for (auto&& provider : m_providers)
            {
                provider->push_success(action);
            }

            m_status[*abi].mark_restored();
        }
    }

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
            Checks::check_exit(VCPKG_LINE_INFO,
                               abi,
                               "Error: package %s did not have an abi during ci. This is an internal error.\n",
                               action.spec);
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
        binary_cache_providers.clear();
        binary_cache_providers.insert("clear");
        interactive = false;
        nugettimeout = "100";
        archives_to_read.clear();
        archives_to_write.clear();
        url_templates_to_get.clear();
        url_templates_to_put.clear();
        gcs_read_prefixes.clear();
        gcs_write_prefixes.clear();
        aws_read_prefixes.clear();
        aws_write_prefixes.clear();
        aws_no_sign_request = false;
        cos_read_prefixes.clear();
        cos_write_prefixes.clear();
        gha_read_prefixes.clear();
        gha_write_prefixes.clear();
        sources_to_read.clear();
        sources_to_write.clear();
        configs_to_read.clear();
        configs_to_write.clear();
        secrets.clear();
    }
}

namespace
{
    const ExpectedS<Path>& default_cache_path()
    {
        static auto cachepath = get_platform_cache_home().then([](Path p) -> ExpectedS<Path> {
            auto maybe_cachepath = get_environment_variable("VCPKG_DEFAULT_BINARY_CACHE");
            if (auto p_str = maybe_cachepath.get())
            {
                get_global_metrics_collector().track_define(DefineMetric::VcpkgDefaultBinaryCache);
                Path path = *p_str;
                path.make_preferred();
                if (!get_real_filesystem().is_directory(path))
                {
                    return {"Value of environment variable VCPKG_DEFAULT_BINARY_CACHE is not a directory: " +
                                path.native(),
                            expected_right_tag};
                }

                if (!path.is_absolute())
                {
                    return {"Value of environment variable VCPKG_DEFAULT_BINARY_CACHE is not absolute: " +
                                path.native(),
                            expected_right_tag};
                }

                return {std::move(path), expected_left_tag};
            }
            p /= "vcpkg/archives";
            p.make_preferred();
            if (p.is_absolute())
            {
                return {std::move(p), expected_left_tag};
            }
            else
            {
                return {"default path was not absolute: " + p.native(), expected_right_tag};
            }
        });
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

                state->interactive = true;
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
                    return add_error(
                        "expected arguments: binary config 'nugettimeout' expects a single positive integer argument");
                }

                auto&& t = segments[1].second;
                if (t.empty())
                {
                    return add_error(
                        "unexpected arguments: binary config 'nugettimeout' requires non-empty nugettimeout");
                }
                char* end;
                long timeout = std::strtol(t.c_str(), &end, 0);
                if (*end != '\0')
                {
                    return add_error("invalid value: binary config 'nugettimeout' requires a valid integer");
                }
                if (timeout <= 0)
                {
                    return add_error("invalid value: binary config 'nugettimeout' requires integers greater than 0");
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
                    return add_error(maybe_home.error(), segments[0].first);
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
                auto headers = azure_blob_headers();
                url_template.headers_for_put.assign(headers.begin(), headers.end());
                handle_readwrite(
                    state->url_templates_to_get, state->url_templates_to_put, std::move(url_template), segments, 3);

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

                auto no_sign_request = false;
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
                // Scheme: x-gha,<prefix>[,<readwrite>]
                if (segments.size() < 2)
                {
                    return add_error(msg::format(msgInvalidArgumentRequiresPrefix, msg::binary_source = "gha"),
                                     segments[0].first);
                }

                if (segments.size() > 3)
                {
                    return add_error(
                        msg::format(msgInvalidArgumentRequiresOneOrTwoArguments, msg::binary_source = "gha"),
                        segments[3].first);
                }

                auto p = segments[1].second;
                if (p.back() != '/')
                {
                    p.push_back('/');
                }

                handle_readwrite(state->gha_read_prefixes, state->gha_write_prefixes, std::move(p), segments, 2);

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
                    url_template.headers_for_get.push_back(segments[3].second);
                    url_template.headers_for_put.push_back(segments[3].second);
                }

                handle_readwrite(
                    state->url_templates_to_get, state->url_templates_to_put, std::move(url_template), segments, 2);
                state->binary_cache_providers.insert("http");
            }
            else
            {
                return add_error(msg::format(msgUnknownBinaryProviderType), segments[0].first);
            }

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
            for (const auto& cache_provider : state->binary_cache_providers)
            {
                auto it = metric_names.find(cache_provider);
                if (it != metric_names.end())
                {
                    metrics.track_define(it->second);
                }
            }

            get_global_metrics_collector().track_submission(std::move(metrics));
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
                    return add_error("unexpected arguments: asset config 'x-block-origin' does not accept arguments",
                                     segments[1].first);
                }
                state->block_origin = true;
            }
            else if (segments[0].second == "clear")
            {
                if (segments.size() != 1)
                {
                    return add_error("unexpected arguments: asset config 'clear' does not take arguments",
                                     segments[1].first);
                }

                state->clear();
            }
            else if (segments[0].second == "x-azurl")
            {
                // Scheme: x-azurl,<baseurl>[,<sas>[,<readwrite>]]
                if (segments.size() < 2)
                {
                    return add_error("expected arguments: asset config 'azurl' requires at least a base url",
                                     segments[0].first);
                }

                if (segments.size() > 4)
                {
                    return add_error("unexpected arguments: asset config 'azurl' requires less than 4 arguments",
                                     segments[4].first);
                }

                if (segments[1].second.empty())
                {
                    return add_error("unexpected arguments: asset config 'azurl' requires a base uri",
                                     segments[1].first);
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
                    return add_error(
                        "expected arguments: asset config 'x-script' requires exactly the exec template as an argument",
                        segments[0].first);
                }
                state->script = segments[1].second;
            }
            else
            {
                return add_error("unknown asset provider type: valid source types are 'x-azurl', "
                                 "'x-script', 'x-block-origin', and 'clear'",
                                 segments[0].first);
            }
        }
    };
}

ExpectedS<DownloadManagerConfig> vcpkg::parse_download_configuration(const Optional<std::string>& arg)
{
    if (!arg || arg.get()->empty()) return DownloadManagerConfig{};

    get_global_metrics_collector().track_define(DefineMetric::AssetSource);

    AssetSourcesState s;
    const auto source = Strings::concat("$", VcpkgCmdArguments::ASSET_SOURCES_ENV);
    AssetSourcesParser parser(*arg.get(), source, &s);
    parser.parse();
    if (auto err = parser.get_error())
    {
        return Strings::concat(err->to_string(), "\nFor more information, see ", docs::assetcaching_url, "\n");
    }

    if (s.azblob_templates_to_put.size() > 1)
    {
        return Strings::concat("Error: a maximum of one asset write url can be specified\n"
                               "For more information, see ",
                               docs::assetcaching_url,
                               "\n");
    }
    if (s.url_templates_to_get.size() > 1)
    {
        return Strings::concat("Error: a maximum of one asset read url can be specified\n"
                               "For more information, see ",
                               docs::assetcaching_url,
                               "\n");
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

ExpectedS<BinaryConfigParserState> vcpkg::create_binary_providers_from_configs_pure(const std::string& env_string,
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

    BinaryConfigParser default_parser("default,readwrite", "<defaults>", &s);
    default_parser.parse();
    if (auto err = default_parser.get_error())
    {
        return err->message;
    }

    BinaryConfigParser env_parser(env_string, "VCPKG_BINARY_SOURCES", &s);
    env_parser.parse();
    if (auto err = env_parser.get_error())
    {
        return err->to_string();
    }

    for (auto&& arg : args)
    {
        BinaryConfigParser arg_parser(arg, "<command>", &s);
        arg_parser.parse();
        if (auto err = arg_parser.get_error())
        {
            return err->to_string();
        }
    }

    return s;
}

ExpectedS<std::vector<std::unique_ptr<IBinaryProvider>>> vcpkg::create_binary_providers_from_configs(
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

    auto sRawHolder = create_binary_providers_from_configs_pure(env_string, args);
    if (!sRawHolder)
    {
        return sRawHolder.error();
    }

    auto& s = sRawHolder.value_or_exit(VCPKG_LINE_INFO);
    std::vector<std::unique_ptr<IBinaryProvider>> providers;
    if (!s.archives_to_read.empty() || !s.archives_to_write.empty() || !s.url_templates_to_put.empty())
    {
        providers.push_back(std::make_unique<ArchivesBinaryProvider>(paths,
                                                                     std::move(s.archives_to_read),
                                                                     std::move(s.archives_to_write),
                                                                     std::move(s.url_templates_to_put),
                                                                     s.secrets));
    }

    if (!s.gcs_read_prefixes.empty() || !s.gcs_write_prefixes.empty())
    {
        providers.push_back(std::make_unique<GcsBinaryProvider>(
            paths, std::move(s.gcs_read_prefixes), std::move(s.gcs_write_prefixes)));
    }

    if (!s.aws_read_prefixes.empty() || !s.aws_write_prefixes.empty())
    {
        providers.push_back(std::make_unique<AwsBinaryProvider>(
            paths, std::move(s.aws_read_prefixes), std::move(s.aws_write_prefixes), s.aws_no_sign_request));
    }

    if (!s.cos_read_prefixes.empty() || !s.cos_write_prefixes.empty())
    {
        providers.push_back(std::make_unique<CosBinaryProvider>(
            paths, std::move(s.cos_read_prefixes), std::move(s.cos_write_prefixes)));
    }

    if (!s.gha_read_prefixes.empty() || !s.gha_write_prefixes.empty())
    {
        providers.push_back(std::make_unique<GHABinaryProvider>(
            paths, std::move(s.gha_read_prefixes), std::move(s.gha_write_prefixes)));
    }

    if (!s.url_templates_to_get.empty())
    {
        providers.push_back(
            std::make_unique<HttpGetBinaryProvider>(paths, std::move(s.url_templates_to_get), s.secrets));
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
                                                                  s.interactive));
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

void vcpkg::help_topic_asset_caching(const VcpkgPaths&)
{
    HelpTableFormatter tbl;
    tbl.text("**Experimental feature: this may change or be removed at any time**");
    tbl.blank();
    tbl.text("Vcpkg can use mirrors to cache downloaded assets, ensuring continued operation even if the original "
             "source changes or disappears.");
    tbl.blank();
    tbl.blank();
    tbl.text(Strings::concat("Asset caching can be configured either by setting the environment variable ",
                             VcpkgCmdArguments::ASSET_SOURCES_ENV,
                             " to a semicolon-delimited list of source strings or by passing a sequence of `--",
                             VcpkgCmdArguments::ASSET_SOURCES_ARG,
                             "=<source>` command line options. Command line sources are interpreted after environment "
                             "sources. Commas, semicolons, and backticks can be escaped using backtick (`)."));
    tbl.blank();
    tbl.blank();
    tbl.header("Valid source strings");
    tbl.format("clear", "Removes all previous sources");
    tbl.format(
        "x-azurl,<url>[,<sas>[,<rw>]]",
        "Adds an Azure Blob Storage source, optionally using Shared Access Signature validation. URL should include "
        "the container path and be terminated with a trailing `/`. SAS, if defined, should be prefixed with a `?`. "
        "Non-Azure servers will also work if they respond to GET and PUT requests of the form: `<url><sha512><sas>`.");
    tbl.format("x-script,<template>",
               "Dispatches to an external tool to fetch the asset. Within the template, \"{url}\" will be replaced by "
               "the original url, \"{sha512}\" will be replaced by the SHA512 value, and \"{dst}\" will be replaced by "
               "the output path to save to. These substitutions will all be properly shell escaped, so an example "
               "template would be: \"curl -L {url} --output {dst}\". \"{{\" will be replaced by \"}\" and \"}}\" will "
               "be replaced by \"}\" to avoid expansion. Note that this will be executed inside the build environment, "
               "so the PATH and other environment variables will be modified by the triplet.");
    tbl.format("x-block-origin",
               "Disables fallback to the original URLs in case the mirror does not have the file available.");
    tbl.blank();
    tbl.text("The `<rw>` optional parameter for certain strings controls how they will be accessed. It can be "
             "specified as `read`, `write`, or `readwrite` and defaults to `read`.");
    tbl.blank();
    print2(tbl.m_str);
    msg::println(msgExtendedDocumentationAtUrl, msg::url = docs::assetcaching_url);
}

void vcpkg::help_topic_binary_caching(const VcpkgPaths&)
{
    HelpTableFormatter tbl;
    tbl.text("Vcpkg can cache compiled packages to accelerate restoration on a single machine or across the network."
             " By default, vcpkg will save builds to a local machine cache. This can be disabled by passing "
             "`--binarysource=clear` as the last option on the command line.");
    tbl.blank();
    tbl.blank();
    tbl.text(
        "Binary caching can be further configured by either passing `--binarysource=<source>` options "
        "to every command line or setting the `VCPKG_BINARY_SOURCES` environment variable to a set of sources (Ex: "
        "\"<source>;<source>;...\"). Command line sources are interpreted after environment sources.");
    tbl.blank();
    tbl.blank();
    tbl.header("Valid source strings");
    tbl.format("clear", "Removes all previous sources");
    tbl.format("default[,<rw>]", "Adds the default file-based location.");
    tbl.format("files,<path>[,<rw>]", "Adds a custom file-based location.");
    tbl.format("http,<url_template>[,<rw>[,<header>]]",
               "Adds a custom http-based location. GET, HEAD and PUT request are done to download, check and upload "
               "the binaries. You can use the variables 'name', 'version', 'sha' and 'triplet'. An example url would "
               "be 'https://cache.example.com/{triplet}/{name}/{version}/{sha}'. Via the header field you can set a "
               "custom header to pass an authorization token.");
    tbl.format("nuget,<uri>[,<rw>]",
               "Adds a NuGet-based source; equivalent to the `-Source` parameter of the NuGet CLI.");
    tbl.format("nugetconfig,<path>[,<rw>]",
               "Adds a NuGet-config-file-based source; equivalent to the `-Config` parameter of the NuGet CLI. This "
               "config should specify `defaultPushSource` for uploads.");
    tbl.format("nugettimeout,<seconds>",
               "Specifies a nugettimeout for NuGet network operations; equivalent to the `-Timeout` parameter of the "
               "NuGet CLI.");
    tbl.format("x-azblob,<url>,<sas>[,<rw>]",
               "**Experimental: will change or be removed without warning** Adds an Azure Blob Storage source. Uses "
               "Shared Access Signature validation. URL should include the container path.");
    tbl.format("x-gcs,<prefix>[,<rw>]",
               "**Experimental: will change or be removed without warning** Adds a Google Cloud Storage (GCS) source. "
               "Uses the gsutil CLI for uploads and downloads. Prefix should include the gs:// scheme and be suffixed "
               "with a `/`.");
    tbl.format("x-aws,<prefix>[,<rw>]",
               "**Experimental: will change or be removed without warning** Adds an AWS S3 source. "
               "Uses the aws CLI for uploads and downloads. Prefix should include s3:// scheme and be suffixed "
               "with a `/`.");
    tbl.format(
        "x-aws-config,<parameter>",
        "**Experimental: will change or be removed without warning** Adds an AWS S3 source. "
        "Adds an AWS configuration; currently supports only 'no-sign-request' parameter that is an equivalent to the "
        "'--no-sign-request parameter of the AWS cli.");
    tbl.format("x-cos,<prefix>[,<rw>]",
               "**Experimental: will change or be removed without warning** Adds an COS source. "
               "Uses the cos CLI for uploads and downloads. Prefix should include cos:// scheme and be suffixed "
               "with a `/`.");
    tbl.format("interactive", "Enables interactive credential management for some source types");
    tbl.blank();
    tbl.text("The `<rw>` optional parameter for certain strings controls whether they will be consulted for "
             "downloading binaries and whether on-demand builds will be uploaded to that remote. It can be specified "
             "as 'read', 'write', or 'readwrite'.");
    tbl.blank();
    tbl.text("The `nuget` and `nugetconfig` source providers additionally respect certain environment variables while "
             "generating nuget packages. The `metadata.repository` field will be optionally generated like:\n"
             "\n"
             "    <repository type=\"git\" url=\"$VCPKG_NUGET_REPOSITORY\"/>\n"
             "or\n"
             "    <repository type=\"git\"\n"
             "                url=\"${GITHUB_SERVER_URL}/${GITHUB_REPOSITORY}.git\"\n"
             "                branch=\"${GITHUB_REF}\"\n"
             "                commit=\"${GITHUB_SHA}\"/>\n"
             "\n"
             "if the appropriate environment variables are defined and non-empty.\n");
    tbl.blank();
    tbl.text("NuGet's cache is not used by default. To use it for every nuget-based source, set the environment "
             "variable `VCPKG_USE_NUGET_CACHE` to `true` (case-insensitive) or `1`.\n");
    tbl.blank();
    print2(tbl.m_str);
    const auto& maybe_cachepath = default_cache_path();
    if (auto p = maybe_cachepath.get())
    {
        msg::println(msgDefaultPathToBinaries, msg::path = *p);
    }

    msg::println(msgExtendedDocumentationAtUrl, msg::url = docs::binarycaching_url);
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
