#include <vcpkg/base/checks.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/binarycaching.private.h>
#include <vcpkg/build.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/metrics.h>
#include <vcpkg/tools.h>

#include <iterator>

using namespace vcpkg;

namespace
{
    static constexpr StringLiteral s_assetcaching_doc_url =
        "https://github.com/Microsoft/vcpkg/tree/master/docs/users/assetcaching.md";
    static constexpr StringLiteral s_binarycaching_doc_url =
        "https://github.com/Microsoft/vcpkg/tree/master/docs/users/binarycaching.md";

    struct ConfigSegmentsParser : Parse::ParserBase
    {
        using Parse::ParserBase::ParserBase;

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
            segments.emplace_back(std::move(loc), std::move(segment));

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

    std::vector<std::vector<std::pair<Parse::ParserBase::SourceLoc, std::string>>> ConfigSegmentsParser::
        parse_all_segments()
    {
        std::vector<std::vector<std::pair<Parse::ParserBase::SourceLoc, std::string>>> ret;
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

    static void clean_prepare_dir(Filesystem& fs, const path& dir)
    {
        fs.remove_all(dir, VCPKG_LINE_INFO);
        bool created_last = fs.create_directories(dir, VCPKG_LINE_INFO);
        Checks::check_exit(VCPKG_LINE_INFO, created_last, "unable to clear path: %s", vcpkg::u8string(dir));
    }

    static ExitCodeAndOutput decompress_archive(const VcpkgPaths& paths, const path& dst, const path& archive_path)
    {
        Command cmd;
#if defined(_WIN32)
        auto&& seven_zip_exe = paths.get_tool_exe(Tools::SEVEN_ZIP);
        cmd.path_arg(seven_zip_exe)
            .string_arg("x")
            .path_arg(archive_path)
            .string_arg("-o" + vcpkg::u8string(dst))
            .string_arg("-y");
#else
        (void)paths;
        cmd.string_arg("unzip").string_arg("-qq").path_arg(archive_path).string_arg("-d" + vcpkg::u8string(dst));
#endif
        return cmd_execute_and_capture_output(cmd, get_clean_environment());
    }

    static ExitCodeAndOutput clean_decompress_archive(const VcpkgPaths& paths,
                                                      const PackageSpec& spec,
                                                      const path& archive_path)
    {
        auto pkg_path = paths.package_dir(spec);
        clean_prepare_dir(paths.get_filesystem(), pkg_path);
        return decompress_archive(paths, pkg_path, archive_path);
    }

    // Compress the source directory into the destination file.
    static void compress_directory(const VcpkgPaths& paths, const path& source, const path& destination)
    {
        auto& fs = paths.get_filesystem();

        std::error_code ec;

        fs.remove(destination, ec);
        Checks::check_exit(
            VCPKG_LINE_INFO, !fs.exists(destination), "Could not remove file: %s", vcpkg::u8string(destination));
#if defined(_WIN32)
        auto&& seven_zip_exe = paths.get_tool_exe(Tools::SEVEN_ZIP);

        cmd_execute_and_capture_output(
            Command{seven_zip_exe}.string_arg("a").path_arg(destination).path_arg(source / vcpkg::u8path("*")),
            get_clean_environment());
#else
        cmd_execute_clean(Command{"zip"}
                              .string_arg("--quiet")
                              .string_arg("-y")
                              .string_arg("-r")
                              .path_arg(destination)
                              .string_arg("*"),
                          InWorkingDirectory{source});
#endif
    }

    static path make_temp_archive_path(const path& buildtrees, const PackageSpec& spec)
    {
        return buildtrees / spec.name() / (spec.triplet().to_string() + ".zip");
    }

    struct ArchivesBinaryProvider : IBinaryProvider
    {
        ArchivesBinaryProvider(std::vector<path>&& read_dirs,
                               std::vector<path>&& write_dirs,
                               std::vector<std::string>&& put_url_templates,
                               std::vector<std::string>&& secrets)
            : m_read_dirs(std::move(read_dirs))
            , m_write_dirs(std::move(write_dirs))
            , m_put_url_templates(std::move(put_url_templates))
            , m_secrets(std::move(secrets))
        {
        }

        static path make_archive_subpath(const std::string& abi)
        {
            return vcpkg::u8path(abi.substr(0, 2)) / vcpkg::u8path(abi + ".zip");
        }

        void prefetch(const VcpkgPaths& paths,
                      View<const Dependencies::InstallPlanAction> actions,
                      View<CacheStatus* const> cache_status) const override
        {
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                auto&& action = actions[idx];
                if (action.has_package_abi() && cache_status[idx]->should_attempt_restore(this))
                {
                    if (try_restore(paths, actions[idx]) == RestoreResult::restored)
                    {
                        cache_status[idx]->mark_restored();
                    }
                }
            }
        }

        RestoreResult try_restore(const VcpkgPaths& paths, const Dependencies::InstallPlanAction& action) const override
        {
            auto& fs = paths.get_filesystem();
            auto& spec = action.spec;
            const auto& abi_tag = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
            const auto archive_subpath = make_archive_subpath(abi_tag);
            for (const auto& archives_root_dir : m_read_dirs)
            {
                auto archive_path = archives_root_dir / archive_subpath;
                if (fs.exists(archive_path))
                {
                    print2("Using cached binary package: ", u8string(archive_path), "\n");

                    int archive_result = clean_decompress_archive(paths, spec, archive_path).exit_code;

                    if (archive_result == 0)
                    {
                        return RestoreResult::restored;
                    }

                    print2("Failed to decompress archive package\n");
                    if (action.build_options.purge_decompress_failure == Build::PurgeDecompressFailure::YES)
                    {
                        print2("Purging bad archive\n");
                        fs.remove(archive_path, ignore_errors);
                    }
                }

                vcpkg::printf("Could not locate cached archive: %s\n", u8string(archive_path));
            }

            return RestoreResult::unavailable;
        }

        void push_success(const VcpkgPaths& paths, const Dependencies::InstallPlanAction& action) const override
        {
            if (m_write_dirs.empty() && m_put_url_templates.empty())
            {
                return;
            }

            const auto& abi_tag = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
            auto& spec = action.spec;
            auto& fs = paths.get_filesystem();
            const auto archive_subpath = make_archive_subpath(abi_tag);
            const auto tmp_archive_path = make_temp_archive_path(paths.buildtrees, spec);
            compress_directory(paths, paths.package_dir(spec), tmp_archive_path);
            size_t http_remotes_pushed = 0;
            for (auto&& put_url_template : m_put_url_templates)
            {
                auto url = Strings::replace_all(std::string(put_url_template), "<SHA>", abi_tag);
                auto maybe_success = Downloads::put_file(fs, url, Downloads::azure_blob_headers(), tmp_archive_path);
                if (maybe_success.has_value())
                {
                    http_remotes_pushed++;
                    continue;
                }

                auto errors = Downloads::replace_secrets(std::move(maybe_success).error(), m_secrets);
                print2(Color::warning, errors);
            }

            if (!m_put_url_templates.empty())
            {
                print2("Uploaded binaries to ", http_remotes_pushed, " HTTP remotes.\n");
            }

            for (const auto& archives_root_dir : m_write_dirs)
            {
                const auto archive_path = archives_root_dir / archive_subpath;
                fs.create_directories(archive_path.parent_path(), ignore_errors);
                std::error_code ec;
                if (m_write_dirs.size() > 1)
                {
                    fs.copy_file(tmp_archive_path, archive_path, copy_options::overwrite_existing, ec);
                }
                else
                {
                    fs.rename_or_copy(tmp_archive_path, archive_path, ".tmp", ec);
                }

                if (ec)
                {
                    vcpkg::printf(
                        Color::warning, "Failed to store binary cache %s: %s\n", u8string(archive_path), ec.message());
                }
                else
                {
                    vcpkg::printf("Stored binary cache: %s\n", vcpkg::u8string(archive_path));
                }
            }
            // In the case of 1 write dir, the file will be moved instead of copied
            if (m_write_dirs.size() != 1)
            {
                fs.remove(tmp_archive_path, ignore_errors);
            }
        }

        void precheck(const VcpkgPaths& paths,
                      View<const Dependencies::InstallPlanAction> actions,
                      View<CacheStatus* const> cache_status) const override
        {
            auto& fs = paths.get_filesystem();
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                const auto& action = actions[idx];
                const auto abi_tag = action.package_abi().get();
                if (!abi_tag || !cache_status[idx]->should_attempt_precheck(this))
                {
                    continue;
                }

                const path archive_subpath = make_archive_subpath(*abi_tag);
                bool any_available = false;
                for (auto&& archives_root_dir : m_read_dirs)
                {
                    if (fs.exists(archives_root_dir / archive_subpath))
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
        std::vector<path> m_read_dirs;
        std::vector<path> m_write_dirs;
        std::vector<std::string> m_put_url_templates;
        std::vector<std::string> m_secrets;
    };
    struct HttpGetBinaryProvider : IBinaryProvider
    {
        HttpGetBinaryProvider(std::vector<std::string>&& url_templates) : m_url_templates(std::move(url_templates)) { }

        RestoreResult try_restore(const VcpkgPaths&, const Dependencies::InstallPlanAction&) const override
        {
            return RestoreResult::unavailable;
        }

        void push_success(const VcpkgPaths&, const Dependencies::InstallPlanAction&) const override { }

        void prefetch(const VcpkgPaths& paths,
                      View<const Dependencies::InstallPlanAction> actions,
                      View<CacheStatus* const> cache_status) const override
        {
            auto& fs = paths.get_filesystem();
            size_t this_restore_count = 0;
            std::vector<std::pair<std::string, path>> url_paths;
            std::vector<size_t> url_indices;
            for (auto&& url_template : m_url_templates)
            {
                url_paths.clear();
                url_indices.clear();
                for (size_t idx = 0; idx < actions.size(); ++idx)
                {
                    auto&& action = actions[idx];
                    auto abi = action.package_abi();
                    if (!abi || !cache_status[idx]->should_attempt_restore(this))
                    {
                        continue;
                    }

                    clean_prepare_dir(fs, paths.package_dir(action.spec));
                    url_paths.emplace_back(Strings::replace_all(url_template, "<SHA>", *abi.get()),
                                           make_temp_archive_path(paths.buildtrees, action.spec));
                    url_indices.push_back(idx);
                }

                if (url_paths.empty()) break;

                print2("Attempting to fetch ", url_paths.size(), " packages from HTTP servers.\n");

                auto codes = Downloads::download_files(fs, url_paths);
                for (size_t i = 0; i < codes.size(); ++i)
                {
                    if (codes[i] == 200)
                    {
                        int archive_result = decompress_archive(paths,
                                                                paths.package_dir(actions[url_indices[i]].spec),
                                                                url_paths[i].second)
                                                 .exit_code;
                        if (archive_result == 0)
                        {
                            // decompression success
                            ++this_restore_count;
                            fs.remove(url_paths[i].second, VCPKG_LINE_INFO);
                            cache_status[url_indices[i]]->mark_restored();
                        }
                        else
                        {
                            Debug::print("Failed to decompress ", vcpkg::u8string(url_paths[i].second), '\n');
                        }
                    }
                }
            }

            print2("Restored ", this_restore_count, " packages from HTTP servers. Use --debug for more information.\n");
        }

        void precheck(const VcpkgPaths&,
                      View<const Dependencies::InstallPlanAction> actions,
                      View<CacheStatus* const> cache_status) const override
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
                    auto abi = actions[idx].package_abi().get();
                    if (!abi || !cache_status[idx]->should_attempt_precheck(this))
                    {
                        continue;
                    }

                    urls.push_back(Strings::replace_all(std::string(url_template), "<SHA>", *abi));
                    url_indices.push_back(idx);
                }

                if (urls.empty())
                {
                    return;
                }

                auto codes = Downloads::url_heads(urls, {});
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

        std::vector<std::string> m_url_templates;
    };

    static std::string trim_leading_zeroes(const std::string& v)
    {
        auto first_non_zero = std::find_if(v.begin(), v.end(), [](char c) { return c != '0'; });
        if (first_non_zero == v.end())
        {
            return std::string(1, '0');
        }

        return std::string(&*first_non_zero, v.end() - first_non_zero);
    }

    struct NugetBinaryProvider : IBinaryProvider
    {
        NugetBinaryProvider(std::vector<std::string>&& read_sources,
                            std::vector<std::string>&& write_sources,
                            std::vector<path>&& read_configs,
                            std::vector<path>&& write_configs,
                            std::string&& timeout,
                            bool interactive)
            : m_read_sources(std::move(read_sources))
            , m_write_sources(std::move(write_sources))
            , m_read_configs(std::move(read_configs))
            , m_write_configs(std::move(write_configs))
            , m_timeout(std::move(timeout))
            , m_interactive(interactive)
            , m_use_nuget_cache(false)
        {
            const std::string use_nuget_cache = get_environment_variable("VCPKG_USE_NUGET_CACHE").value_or("");
            m_use_nuget_cache = Strings::case_insensitive_ascii_equals(use_nuget_cache, "true") ||
                                Strings::case_insensitive_ascii_equals(use_nuget_cache, "1");
        }

        int run_nuget_commandline(const Command& cmdline) const
        {
            if (m_interactive)
            {
                return cmd_execute(cmdline);
            }

            auto res = cmd_execute_and_capture_output(cmdline);
            if (Debug::g_debugging)
            {
                print2(res.output);
            }

            if (res.output.find("Authentication may require manual action.") != std::string::npos)
            {
                print2(Color::warning,
                       "One or more NuGet credential providers requested manual action. Add the binary "
                       "source 'interactive' to allow interactivity.\n");
            }
            else if (res.output.find("Response status code does not indicate success: 401 (Unauthorized)") !=
                         std::string::npos &&
                     res.exit_code != 0)
            {
                print2(Color::warning,
                       "One or more NuGet credential providers failed to authenticate. See "
                       "https://github.com/Microsoft/vcpkg/tree/master/docs/users/binarycaching.md for "
                       "more details on how to provide credentials.\n");
            }
            else if (res.output.find("for example \"-ApiKey AzureDevOps\"") != std::string::npos)
            {
                auto real_cmdline = cmdline;
                real_cmdline.string_arg("-ApiKey").string_arg("AzureDevOps");
                auto res2 = cmd_execute_and_capture_output(real_cmdline);
                if (Debug::g_debugging)
                {
                    print2(res2.output);
                }

                return res2.exit_code;
            }

            return res.exit_code;
        }

        struct NuGetPrefetchAttempt
        {
            PackageSpec spec;
            NugetReference reference;
            size_t result_index;
        };

        static void generate_packages_config(Filesystem& fs,
                                             const path& packages_config,
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

        void prefetch(const VcpkgPaths& paths,
                      View<const Dependencies::InstallPlanAction> actions,
                      View<CacheStatus* const> cache_status) const override
        {
            if (m_read_sources.empty() && m_read_configs.empty())
            {
                return;
            }

            auto& fs = paths.get_filesystem();

            std::vector<NuGetPrefetchAttempt> attempts;
            for (size_t idx = 0; idx < actions.size(); ++idx)
            {
                auto&& action = actions[idx];
                auto abi = action.package_abi().get();
                if (!abi || !cache_status[idx]->should_attempt_restore(this))
                {
                    continue;
                }

                auto& spec = action.spec;
                fs.remove_all(paths.package_dir(spec), VCPKG_LINE_INFO);
                attempts.push_back({spec, make_nugetref(action, get_nuget_prefix()), idx});
            }

            if (attempts.empty())
            {
                return;
            }

            print2("Attempting to fetch ", attempts.size(), " packages from nuget.\n");

            auto packages_config = paths.buildtrees / vcpkg::u8path("packages.config");
            const auto& nuget_exe = paths.get_tool_exe("nuget");
            std::vector<Command> cmdlines;

            if (!m_read_sources.empty())
            {
                // First check using all sources
                Command cmdline;
#ifndef _WIN32
                cmdline.path_arg(paths.get_tool_exe(Tools::MONO));
#endif
                cmdline.path_arg(nuget_exe)
                    .string_arg("install")
                    .path_arg(packages_config)
                    .string_arg("-OutputDirectory")
                    .path_arg(paths.packages)
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
                cmdline.path_arg(paths.get_tool_exe(Tools::MONO));
#endif
                cmdline.path_arg(nuget_exe)
                    .string_arg("install")
                    .path_arg(packages_config)
                    .string_arg("-OutputDirectory")
                    .path_arg(paths.packages)
                    .string_arg("-ConfigFile")
                    .path_arg(cfg)
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
                    auto nupkg_path =
                        paths.package_dir(nuget_ref.spec) / vcpkg::u8path(nuget_ref.reference.id + ".nupkg");
                    if (fs.exists(nupkg_path, ignore_errors))
                    {
                        fs.remove(nupkg_path, VCPKG_LINE_INFO);
                        Checks::check_exit(VCPKG_LINE_INFO,
                                           !fs.exists(nupkg_path, ignore_errors),
                                           "Unable to remove nupkg after restoring: %s",
                                           u8string(nupkg_path));
                        cache_status[nuget_ref.result_index]->mark_restored();
                        return true;
                    }

                    return false;
                });
            }

            print2("Restored ",
                   total_restore_attempts - attempts.size(),
                   " packages from NuGet. Use --debug for more information.\n");
        }

        RestoreResult try_restore(const VcpkgPaths&, const Dependencies::InstallPlanAction&) const override
        {
            return RestoreResult::unavailable;
        }

        void push_success(const VcpkgPaths& paths, const Dependencies::InstallPlanAction& action) const override
        {
            if (m_write_sources.empty() && m_write_configs.empty())
            {
                return;
            }

            auto& spec = action.spec;

            NugetReference nuget_ref = make_nugetref(action, get_nuget_prefix());
            auto nuspec_path = paths.buildtrees / spec.name() / (spec.triplet().to_string() + ".nuspec");
            paths.get_filesystem().write_contents(
                nuspec_path, generate_nuspec(paths, action, nuget_ref), VCPKG_LINE_INFO);

            const auto& nuget_exe = paths.get_tool_exe("nuget");
            Command cmdline;
#ifndef _WIN32
            cmdline.path_arg(paths.get_tool_exe(Tools::MONO));
#endif
            cmdline.path_arg(nuget_exe)
                .string_arg("pack")
                .path_arg(nuspec_path)
                .string_arg("-OutputDirectory")
                .path_arg(paths.buildtrees)
                .string_arg("-NoDefaultExcludes")
                .string_arg("-ForceEnglishOutput");

            if (!m_interactive)
            {
                cmdline.string_arg("-NonInteractive");
            }

            if (run_nuget_commandline(cmdline) != 0)
            {
                print2(Color::error, "Packing NuGet failed. Use --debug for more information.\n");
                return;
            }

            auto nupkg_path = paths.buildtrees / nuget_ref.nupkg_filename();
            for (auto&& write_src : m_write_sources)
            {
                Command cmd;
#ifndef _WIN32
                cmd.path_arg(paths.get_tool_exe(Tools::MONO));
#endif
                cmd.path_arg(nuget_exe)
                    .string_arg("push")
                    .path_arg(nupkg_path)
                    .string_arg("-ForceEnglishOutput")
                    .string_arg("-Timeout")
                    .string_arg(m_timeout)
                    .string_arg("-Source")
                    .string_arg(write_src);

                if (!m_interactive)
                {
                    cmd.string_arg("-NonInteractive");
                }

                print2("Uploading binaries for ", spec, " to NuGet source ", write_src, ".\n");
                if (run_nuget_commandline(cmd) != 0)
                {
                    print2(
                        Color::error, "Pushing NuGet to ", write_src, " failed. Use --debug for more information.\n");
                }
            }
            for (auto&& write_cfg : m_write_configs)
            {
                Command cmd;
#ifndef _WIN32
                cmd.path_arg(paths.get_tool_exe(Tools::MONO));
#endif
                cmd.path_arg(nuget_exe)
                    .string_arg("push")
                    .path_arg(nupkg_path)
                    .string_arg("-ForceEnglishOutput")
                    .string_arg("-Timeout")
                    .string_arg(m_timeout)
                    .string_arg("-ConfigFile")
                    .path_arg(write_cfg);
                if (!m_interactive)
                {
                    cmd.string_arg("-NonInteractive");
                }

                print2("Uploading binaries for ", spec, " using NuGet config ", u8string(write_cfg), ".\n");

                if (run_nuget_commandline(cmd) != 0)
                {
                    print2(Color::error,
                           "Pushing NuGet with ",
                           u8string(write_cfg),
                           " failed. Use --debug for more information.\n");
                }
            }

            paths.get_filesystem().remove(nupkg_path, ignore_errors);
        }

        void precheck(const VcpkgPaths&,
                      View<const Dependencies::InstallPlanAction>,
                      View<CacheStatus* const>) const override
        {
        }

    private:
        std::vector<std::string> m_read_sources;
        std::vector<std::string> m_write_sources;

        std::vector<path> m_read_configs;
        std::vector<path> m_write_configs;

        std::string m_timeout;
        bool m_interactive;
        bool m_use_nuget_cache;
    };

    bool gsutil_stat(const std::string& url)
    {
        Command cmd;
        cmd.string_arg("gsutil").string_arg("-q").string_arg("stat").string_arg(url);
        const auto res = cmd_execute(cmd);
        return res == 0;
    }

    bool gsutil_upload_file(const std::string& gcs_object, const path& archive)
    {
        Command cmd;
        cmd.string_arg("gsutil").string_arg("-q").string_arg("cp").path_arg(archive).string_arg(gcs_object);
        const auto out = cmd_execute_and_capture_output(cmd);
        if (out.exit_code == 0)
        {
            return true;
        }

        print2(Color::warning, "gsutil failed to upload with exit code: ", out.exit_code, '\n', out.output);
        return false;
    }

    bool gsutil_download_file(const std::string& gcs_object, const path& archive)
    {
        Command cmd;
        cmd.string_arg("gsutil").string_arg("-q").string_arg("cp").string_arg(gcs_object).path_arg(archive);
        const auto out = cmd_execute_and_capture_output(cmd);
        if (out.exit_code == 0)
        {
            return true;
        }

        print2(Color::warning, "gsutil failed to download with exit code: ", out.exit_code, '\n', out.output);
        return false;
    }

    struct GcsBinaryProvider : IBinaryProvider
    {
        GcsBinaryProvider(std::vector<std::string>&& read_prefixes, std::vector<std::string>&& write_prefixes)
            : m_read_prefixes(std::move(read_prefixes)), m_write_prefixes(std::move(write_prefixes))
        {
        }

        static std::string make_gcs_path(const std::string& prefix, const std::string& abi)
        {
            return Strings::concat(prefix, abi, ".zip");
        }

        void prefetch(const VcpkgPaths& paths,
                      View<const Dependencies::InstallPlanAction> actions,
                      View<CacheStatus* const> cache_status) const override
        {
            auto& fs = paths.get_filesystem();

            size_t restored_count = 0;
            for (const auto& prefix : m_read_prefixes)
            {
                std::vector<std::pair<std::string, path>> url_paths;
                std::vector<size_t> url_indices;

                for (size_t idx = 0; idx < actions.size(); ++idx)
                {
                    auto&& action = actions[idx];
                    auto abi = action.package_abi().get();
                    if (!abi || !cache_status[idx]->should_attempt_restore(this))
                    {
                        continue;
                    }

                    clean_prepare_dir(fs, paths.package_dir(action.spec));
                    url_paths.emplace_back(make_gcs_path(prefix, *abi),
                                           make_temp_archive_path(paths.buildtrees, action.spec));
                    url_indices.push_back(idx);
                }

                if (url_paths.empty()) break;

                print2("Attempting to fetch ", url_paths.size(), " packages from GCS.\n");
                for (size_t idx = 0; idx < url_paths.size(); ++idx)
                {
                    auto&& action = actions[url_indices[idx]];
                    auto&& url_path = url_paths[idx];
                    if (!gsutil_download_file(url_path.first, url_path.second)) continue;
                    if (decompress_archive(paths, paths.package_dir(action.spec), url_path.second).exit_code != 0)
                    {
                        Debug::print("Failed to decompress ", u8string(url_path.second), '\n');
                        continue;
                    }

                    // decompression success
                    ++restored_count;
                    fs.remove(url_path.second, VCPKG_LINE_INFO);
                    cache_status[url_indices[idx]]->mark_restored();
                }
            }

            print2("Restored ", restored_count, " packages from GCS servers. Use --debug for more information.\n");
        }

        RestoreResult try_restore(const VcpkgPaths&, const Dependencies::InstallPlanAction&) const override
        {
            return RestoreResult::unavailable;
        }

        void push_success(const VcpkgPaths& paths, const Dependencies::InstallPlanAction& action) const override
        {
            if (m_write_prefixes.empty()) return;
            const auto& abi = action.package_abi().value_or_exit(VCPKG_LINE_INFO);
            auto& spec = action.spec;
            const auto tmp_archive_path = make_temp_archive_path(paths.buildtrees, spec);
            compress_directory(paths, paths.package_dir(spec), tmp_archive_path);

            size_t upload_count = 0;
            for (const auto& prefix : m_write_prefixes)
            {
                if (gsutil_upload_file(make_gcs_path(prefix, abi), tmp_archive_path))
                {
                    ++upload_count;
                }
            }

            print2("Uploaded binaries to ", upload_count, " GCS remotes.\n");
        }

        void precheck(const VcpkgPaths&,
                      View<const Dependencies::InstallPlanAction> actions,
                      View<CacheStatus* const> cache_status) const override
        {
            std::vector<CacheAvailability> actions_availability{actions.size()};
            for (const auto& prefix : m_read_prefixes)
            {
                for (size_t idx = 0; idx < actions.size(); ++idx)
                {
                    auto&& action = actions[idx];
                    const auto abi = action.package_abi().get();
                    if (!abi || !cache_status[idx]->should_attempt_precheck(this))
                    {
                        continue;
                    }

                    if (gsutil_stat(make_gcs_path(prefix, *abi)))
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

    private:
        std::vector<std::string> m_read_prefixes;
        std::vector<std::string> m_write_prefixes;
    };
}

namespace vcpkg
{
    BinaryCache::BinaryCache(const VcpkgCmdArguments& args) { install_providers_for(args); }

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

    void BinaryCache::install_providers_for(const VcpkgCmdArguments& args)
    {
        if (args.binary_caching_enabled())
        {
            install_providers(create_binary_providers_from_configs(args.binary_sources).value_or_exit(VCPKG_LINE_INFO));
        }
    }

    RestoreResult BinaryCache::try_restore(const VcpkgPaths& paths, const Dependencies::InstallPlanAction& action)
    {
        const auto abi = action.package_abi().get();
        if (!abi)
        {
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
            switch (available->try_restore(paths, action))
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

            switch (provider->try_restore(paths, action))
            {
                case RestoreResult::restored: cache_status.mark_restored(); return RestoreResult::restored;
                case RestoreResult::unavailable: cache_status.mark_unavailable(provider.get()); break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        return RestoreResult::unavailable;
    }

    void BinaryCache::push_success(const VcpkgPaths& paths, const Dependencies::InstallPlanAction& action)
    {
        const auto abi = action.package_abi().get();
        if (abi)
        {
            for (auto&& provider : m_providers)
            {
                provider->push_success(paths, action);
            }

            m_status[*abi].mark_restored();
        }
    }

    static std::vector<CacheStatus*> build_cache_status_vector(View<const Dependencies::InstallPlanAction> actions,
                                                               std::unordered_map<std::string, CacheStatus>& status)
    {
        std::vector<CacheStatus*> results{actions.size()};
        for (size_t idx = 0; idx < actions.size(); ++idx)
        {
            const auto abi = actions[idx].package_abi().get();
            if (abi)
            {
                results[idx] = &status[*abi];
            }
        }

        return results;
    }

    void BinaryCache::prefetch(const VcpkgPaths& paths, View<const Dependencies::InstallPlanAction> actions)
    {
        auto cache_status = build_cache_status_vector(actions, m_status);
        for (auto&& provider : m_providers)
        {
            provider->prefetch(paths, actions, cache_status);
            for (auto status : cache_status)
            {
                if (status)
                {
                    status->mark_unavailable(provider.get());
                }
            }
        }
    }

    std::vector<CacheAvailability> BinaryCache::precheck(const VcpkgPaths& paths,
                                                         View<const Dependencies::InstallPlanAction> actions)
    {
        auto cache_status = build_cache_status_vector(actions, m_status);
        for (auto&& provider : m_providers)
        {
            provider->precheck(paths, actions, cache_status);
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
}

namespace
{
    const ExpectedS<path>& default_cache_path()
    {
        static auto cachepath = get_platform_cache_home().then([](path p) -> ExpectedS<path> {
            auto maybe_cachepath = get_environment_variable("VCPKG_DEFAULT_BINARY_CACHE");
            if (auto p_str = maybe_cachepath.get())
            {
                Metrics::g_metrics.lock()->track_property("VCPKG_DEFAULT_BINARY_CACHE", "defined");
                auto path = vcpkg::u8path(*p_str);
                path.make_preferred();
                const auto status = stdfs::status(path);
                if (!stdfs::exists(status))
                {
                    return {"Path to VCPKG_DEFAULT_BINARY_CACHE does not exist: " + vcpkg::u8string(path),
                            expected_right_tag};
                }

                if (!stdfs::is_directory(status))
                {
                    return {"Value of environment variable VCPKG_DEFAULT_BINARY_CACHE is not a directory: " +
                                vcpkg::u8string(path),
                            expected_right_tag};
                }

                if (!path.is_absolute())
                {
                    return {"Value of environment variable VCPKG_DEFAULT_BINARY_CACHE is not absolute: " +
                                vcpkg::u8string(path),
                            expected_right_tag};
                }

                return {std::move(path), expected_left_tag};
            }
            p /= vcpkg::u8path("vcpkg/archives");
            p.make_preferred();
            if (p.is_absolute())
            {
                return {std::move(p), expected_left_tag};
            }
            else
            {
                return {"default path was not absolute: " + vcpkg::u8string(p), expected_right_tag};
            }
        });
        return cachepath;
    }

    struct State
    {
        bool m_cleared = false;
        bool interactive = false;
        std::string nugettimeout = "100";

        std::vector<path> archives_to_read;
        std::vector<path> archives_to_write;

        std::vector<std::string> url_templates_to_get;
        std::vector<std::string> azblob_templates_to_put;

        std::vector<std::string> gcs_read_prefixes;
        std::vector<std::string> gcs_write_prefixes;

        std::vector<std::string> sources_to_read;
        std::vector<std::string> sources_to_write;

        std::vector<path> configs_to_read;
        std::vector<path> configs_to_write;

        std::vector<std::string> secrets;

        void clear()
        {
            m_cleared = true;
            interactive = false;
            nugettimeout = "100";
            archives_to_read.clear();
            archives_to_write.clear();
            url_templates_to_get.clear();
            azblob_templates_to_put.clear();
            gcs_read_prefixes.clear();
            gcs_write_prefixes.clear();
            sources_to_read.clear();
            sources_to_write.clear();
            configs_to_read.clear();
            configs_to_write.clear();
            secrets.clear();
        }
    };

    struct BinaryConfigParser : ConfigSegmentsParser
    {
        BinaryConfigParser(StringView text, StringView origin, State* state)
            : ConfigSegmentsParser(text, origin), state(state)
        {
        }

        State* state;

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
                    return add_error("unexpected arguments: binary config 'clear' does not take arguments",
                                     segments[1].first);
                }

                state->clear();
            }
            else if (segments[0].second == "files")
            {
                if (segments.size() < 2)
                {
                    return add_error("expected arguments: binary config 'files' requires at least a path argument",
                                     segments[0].first);
                }

                auto p = vcpkg::u8path(segments[1].second);
                if (!p.is_absolute())
                {
                    return add_error("expected arguments: path arguments for binary config strings must be absolute",
                                     segments[1].first);
                }

                handle_readwrite(state->archives_to_read, state->archives_to_write, std::move(p), segments, 2);
                if (segments.size() > 3)
                {
                    return add_error("unexpected arguments: binary config 'files' requires 1 or 2 arguments",
                                     segments[3].first);
                }
            }
            else if (segments[0].second == "interactive")
            {
                if (segments.size() > 1)
                {
                    return add_error("unexpected arguments: binary config 'interactive' does not accept any arguments",
                                     segments[1].first);
                }

                state->interactive = true;
            }
            else if (segments[0].second == "nugetconfig")
            {
                if (segments.size() < 2)
                {
                    return add_error(
                        "expected arguments: binary config 'nugetconfig' requires at least a source argument",
                        segments[0].first);
                }

                auto p = vcpkg::u8path(segments[1].second);
                if (!p.is_absolute())
                {
                    return add_error("expected arguments: path arguments for binary config strings must be absolute",
                                     segments[1].first);
                }

                handle_readwrite(state->configs_to_read, state->configs_to_write, std::move(p), segments, 2);
                if (segments.size() > 3)
                {
                    return add_error("unexpected arguments: binary config 'nugetconfig' requires 1 or 2 arguments",
                                     segments[3].first);
                }
            }
            else if (segments[0].second == "nuget")
            {
                if (segments.size() < 2)
                {
                    return add_error("expected arguments: binary config 'nuget' requires at least a source argument",
                                     segments[0].first);
                }

                auto&& p = segments[1].second;
                if (p.empty())
                {
                    return add_error("unexpected arguments: binary config 'nuget' requires non-empty source");
                }

                handle_readwrite(state->sources_to_read, state->sources_to_write, std::move(p), segments, 2);
                if (segments.size() > 3)
                {
                    return add_error("unexpected arguments: binary config 'nuget' requires 1 or 2 arguments",
                                     segments[3].first);
                }
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
            }
            else if (segments[0].second == "default")
            {
                if (segments.size() > 2)
                {
                    return add_error("unexpected arguments: binary config 'default' does not take more than 1 argument",
                                     segments[0].first);
                }

                const auto& maybe_home = default_cache_path();
                if (!maybe_home.has_value())
                {
                    return add_error(maybe_home.error(), segments[0].first);
                }

                handle_readwrite(
                    state->archives_to_read, state->archives_to_write, path(*maybe_home.get()), segments, 1);
            }
            else if (segments[0].second == "x-azblob")
            {
                // Scheme: x-azblob,<baseurl>,<sas>[,<readwrite>]
                if (segments.size() < 3)
                {
                    return add_error(
                        "expected arguments: binary config 'azblob' requires at least a base-url and a SAS token",
                        segments[0].first);
                }

                if (!Strings::starts_with(segments[1].second, "https://"))
                {
                    return add_error(
                        "invalid argument: binary config 'azblob' requires an https base url as the first argument",
                        segments[1].first);
                }

                if (Strings::starts_with(segments[2].second, "?"))
                {
                    return add_error("invalid argument: binary config 'azblob' requires a SAS token without a "
                                     "preceeding '?' as the second argument",
                                     segments[2].first);
                }

                if (segments.size() > 4)
                {
                    return add_error("unexpected arguments: binary config 'azblob' requires 2 or 3 arguments",
                                     segments[4].first);
                }

                auto p = segments[1].second;
                if (p.back() != '/')
                {
                    p.push_back('/');
                }

                p.append("<SHA>.zip");
                if (!Strings::starts_with(segments[2].second, "?"))
                {
                    p.push_back('?');
                }

                p.append(segments[2].second);
                state->secrets.push_back(segments[2].second);
                handle_readwrite(
                    state->url_templates_to_get, state->azblob_templates_to_put, std::move(p), segments, 3);
            }
            else if (segments[0].second == "x-gcs")
            {
                // Scheme: x-gcs,<prefix>[,<readwrite>]
                if (segments.size() < 2)
                {
                    return add_error("expected arguments: binary config 'gcs' requires at least a prefix",
                                     segments[0].first);
                }

                if (!Strings::starts_with(segments[1].second, "gs://"))
                {
                    return add_error(
                        "invalid argument: binary config 'gcs' requires a gs:// base url as the first argument",
                        segments[1].first);
                }

                if (segments.size() > 3)
                {
                    return add_error("unexpected arguments: binary config 'gcs' requires 1 or 2 arguments",
                                     segments[3].first);
                }

                auto p = segments[1].second;
                if (p.back() != '/')
                {
                    p.push_back('/');
                }

                handle_readwrite(state->gcs_read_prefixes, state->gcs_write_prefixes, std::move(p), segments, 2);
            }
            else
            {
                return add_error(
                    "unknown binary provider type: valid providers are 'clear', 'default', 'nuget', 'nugetconfig', "
                    "'interactive', and 'files'",
                    segments[0].first);
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

        void clear()
        {
            cleared = true;
            block_origin = false;
            url_templates_to_get.clear();
            azblob_templates_to_put.clear();
            secrets.clear();
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
            else
            {
                return add_error(
                    "unknown asset provider type: valid source types are 'x-azurl', 'x-block-origin', and 'clear'",
                    segments[0].first);
            }
        }
    };
}

ExpectedS<Downloads::DownloadManagerConfig> vcpkg::parse_download_configuration(const Optional<std::string>& arg)
{
    if (!arg || arg.get()->empty()) return Downloads::DownloadManagerConfig{};

    Metrics::g_metrics.lock()->track_property("asset-source", "defined");

    AssetSourcesState s;
    AssetSourcesParser parser(*arg.get(), Strings::concat("$", VcpkgCmdArguments::ASSET_SOURCES_ENV), &s);
    parser.parse();
    if (auto err = parser.get_error())
    {
        return Strings::concat(err->format(), "For more information, see ", s_assetcaching_doc_url, "\n");
    }

    if (s.azblob_templates_to_put.size() > 1)
    {
        return Strings::concat("Error: a maximum of one asset write url can be specified\n"
                               "For more information, see ",
                               s_assetcaching_doc_url,
                               "\n");
    }
    if (s.url_templates_to_get.size() > 1)
    {
        return Strings::concat("Error: a maximum of one asset read url can be specified\n"
                               "For more information, see ",
                               s_assetcaching_doc_url,
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
        auto v = Downloads::azure_blob_headers();
        put_headers.assign(v.begin(), v.end());
    }

    return Downloads::DownloadManagerConfig{std::move(get_url),
                                            std::vector<std::string>{},
                                            std::move(put_url),
                                            std::move(put_headers),
                                            std::move(s.secrets),
                                            s.block_origin};
}

ExpectedS<std::vector<std::unique_ptr<IBinaryProvider>>> vcpkg::create_binary_providers_from_configs(
    View<std::string> args)
{
    std::string env_string = get_environment_variable("VCPKG_BINARY_SOURCES").value_or("");
    if (Debug::g_debugging)
    {
        const auto& cachepath = default_cache_path();
        if (cachepath.has_value())
        {
            Debug::print("Default binary cache path is: ", vcpkg::u8string(*cachepath.get()), '\n');
        }
        else
        {
            Debug::print("No binary cache path. Reason: ", cachepath.error(), '\n');
        }
    }

    return create_binary_providers_from_configs_pure(env_string, args);
}

ExpectedS<std::vector<std::unique_ptr<IBinaryProvider>>> vcpkg::create_binary_providers_from_configs_pure(
    const std::string& env_string, View<std::string> args)
{
    {
        auto metrics = Metrics::g_metrics.lock();
        if (!env_string.empty())
        {
            metrics->track_property("VCPKG_BINARY_SOURCES", "defined");
        }

        if (args.size() != 0)
        {
            metrics->track_property("binarycaching-source", "defined");
        }
    }

    State s;

    BinaryConfigParser default_parser("default,readwrite", "<defaults>", &s);
    default_parser.parse();
    if (auto err = default_parser.get_error())
    {
        return err->get_message();
    }

    BinaryConfigParser env_parser(env_string, "VCPKG_BINARY_SOURCES", &s);
    env_parser.parse();
    if (auto err = env_parser.get_error())
    {
        return err->format();
    }

    for (auto&& arg : args)
    {
        BinaryConfigParser arg_parser(arg, "<command>", &s);
        arg_parser.parse();
        if (auto err = arg_parser.get_error())
        {
            return err->format();
        }
    }

    if (s.m_cleared)
    {
        Metrics::g_metrics.lock()->track_property("binarycaching-clear", "defined");
    }

    std::vector<std::unique_ptr<IBinaryProvider>> providers;
    if (!s.gcs_read_prefixes.empty() || !s.gcs_write_prefixes.empty())
    {
        providers.push_back(
            std::make_unique<GcsBinaryProvider>(std::move(s.gcs_read_prefixes), std::move(s.gcs_write_prefixes)));
    }

    if (!s.archives_to_read.empty() || !s.archives_to_write.empty() || !s.azblob_templates_to_put.empty())
    {
        providers.push_back(std::make_unique<ArchivesBinaryProvider>(std::move(s.archives_to_read),
                                                                     std::move(s.archives_to_write),
                                                                     std::move(s.azblob_templates_to_put),
                                                                     std::move(s.secrets)));
    }

    if (!s.url_templates_to_get.empty())
    {
        Metrics::g_metrics.lock()->track_property("binarycaching-url-get", "defined");
        providers.push_back(std::make_unique<HttpGetBinaryProvider>(std::move(s.url_templates_to_get)));
    }

    if (!s.sources_to_read.empty() || !s.sources_to_write.empty() || !s.configs_to_read.empty() ||
        !s.configs_to_write.empty())
    {
        Metrics::g_metrics.lock()->track_property("binarycaching-nuget", "defined");
        providers.push_back(std::make_unique<NugetBinaryProvider>(std::move(s.sources_to_read),
                                                                  std::move(s.sources_to_write),
                                                                  std::move(s.configs_to_read),
                                                                  std::move(s.configs_to_write),
                                                                  std::move(s.nugettimeout),
                                                                  s.interactive));
    }

    return providers;
}

std::string vcpkg::reformat_version(const std::string& version, const std::string& abi_tag)
{
    static const std::regex semver_matcher(R"(v?(\d+)(\.\d+|$)(\.\d+)?.*)");

    std::smatch sm;
    if (std::regex_match(version.cbegin(), version.cend(), sm, semver_matcher))
    {
        auto major = trim_leading_zeroes(sm.str(1));
        auto minor = sm.size() > 2 && !sm.str(2).empty() ? trim_leading_zeroes(sm.str(2).substr(1)) : "0";
        auto patch = sm.size() > 3 && !sm.str(3).empty() ? trim_leading_zeroes(sm.str(3).substr(1)) : "0";
        return Strings::concat(major, '.', minor, '.', patch, "-vcpkg", abi_tag);
    }

    static const std::regex date_matcher(R"((\d\d\d\d)-(\d\d)-(\d\d).*)");
    if (std::regex_match(version.cbegin(), version.cend(), sm, date_matcher))
    {
        return Strings::concat(trim_leading_zeroes(sm.str(1)),
                               '.',
                               trim_leading_zeroes(sm.str(2)),
                               '.',
                               trim_leading_zeroes(sm.str(3)),
                               "-vcpkg",
                               abi_tag);
    }

    return Strings::concat("0.0.0-vcpkg", abi_tag);
}

details::NuGetRepoInfo details::get_nuget_repo_info_from_env()
{
    auto vcpkg_nuget_repository = get_environment_variable("VCPKG_NUGET_REPOSITORY");
    if (auto p = vcpkg_nuget_repository.get())
    {
        Metrics::g_metrics.lock()->track_property("VCPKG_NUGET_REPOSITORY", "defined");
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

    Metrics::g_metrics.lock()->track_property("GITHUB_REPOSITORY", "defined");
    return {Strings::concat(gh_server, '/', gh_repo, ".git"),
            get_environment_variable("GITHUB_REF").value_or(""),
            get_environment_variable("GITHUB_SHA").value_or("")};
}

std::string vcpkg::generate_nuspec(const VcpkgPaths& paths,
                                   const Dependencies::InstallPlanAction& action,
                                   const vcpkg::NugetReference& ref,
                                   details::NuGetRepoInfo rinfo)
{
    auto& spec = action.spec;
    auto& scf = *action.source_control_file_location.value_or_exit(VCPKG_LINE_INFO).source_control_file;
    auto& version = scf.core_paragraph->version;
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
        .text_attr("src", vcpkg::u8string(paths.package_dir(spec) / vcpkg::u8path("**")))
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
    tbl.text(Strings::concat(
        "Asset caching can be configured by setting the environment variable ",
        VcpkgCmdArguments::ASSET_SOURCES_ENV,
        " to a semicolon-delimited list of source strings. Characters can be escaped using backtick (`)."));
    tbl.blank();
    tbl.blank();
    tbl.header("Valid source strings");
    tbl.format("clear", "Removes all previous sources");
    tbl.format(
        "x-azurl,<url>[,<sas>[,<rw>]]",
        "Adds an Azure Blob Storage source, optionally using Shared Access Signature validation. URL should "
        "include "
        "the container path and be terminated with a trailing `/`. SAS, if defined, should be prefixed with a `?`. "
        "Non-Azure servers will also work if they respond to GET and PUT requests of the form: "
        "`<url><sha512><sas>`.");
    tbl.format("x-block-origin",
               "Disables use of the original URLs in case the mirror does not have the file available.");
    tbl.blank();
    tbl.text("The `<rw>` optional parameter for certain strings controls how they will be accessed. It can be "
             "specified as `read`, `write`, or `readwrite` and defaults to `read`.");
    tbl.blank();
    print2(tbl.m_str);

    print2("\nExtended documentation is available at ", s_assetcaching_doc_url, "\n");
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
        print2(
            "\nBased on your system settings, the default path to store binaries is\n    ",
            vcpkg::u8string(*p),
            "\nThis consults %LOCALAPPDATA%/%APPDATA% on Windows and $XDG_CACHE_HOME or $HOME on other platforms.\n");
    }

    print2("\nExtended documentation is available at ", s_binarycaching_doc_url, "\n");
}

std::string vcpkg::generate_nuget_packages_config(const Dependencies::ActionPlan& action)
{
    auto refs = Util::fmap(action.install_actions, [&](const Dependencies::InstallPlanAction& ipa) {
        return make_nugetref(ipa, get_nuget_prefix());
    });
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
