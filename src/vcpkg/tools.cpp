#include <vcpkg/base/cache.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/archives.h>
#include <vcpkg/tools.h>
#include <vcpkg/tools.test.h>
#include <vcpkg/versions.h>

#include <map>

#include <fmt/ranges.h>

namespace
{
    using namespace vcpkg;
    struct ToolOsEntry
    {
        StringLiteral name;
        ToolOs os;
    };

    // keep this in sync with vcpkg-tools.schema.json
    static constexpr ToolOsEntry all_tool_oses[] = {
        {"windows", ToolOs::Windows},
        {"osx", ToolOs::Osx},
        {"linux", ToolOs::Linux},
        {"freebsd", ToolOs::FreeBsd},
        {"openbsd", ToolOs::OpenBsd},
        {"netbsd", ToolOs::NetBsd},
        {"solaris", ToolOs::Solaris},
    };
}

namespace vcpkg
{
    // /\d+(\.\d+(\.\d+)?)?/
    Optional<std::array<int, 3>> parse_tool_version_string(StringView string_version)
    {
        // first, find the beginning of the version
        auto first = string_version.begin();
        const auto last = string_version.end();

        // we're looking for the first instance of `<digits>.<digits>`
        ParsedExternalVersion parsed_version{};
        for (;;)
        {
            first = std::find_if(first, last, ParserBase::is_ascii_digit);
            if (first == last)
            {
                return nullopt;
            }

            if (try_extract_external_dot_version(parsed_version, StringView{first, last}) &&
                !parsed_version.minor.empty())
            {
                break;
            }

            first = std::find_if_not(first, last, ParserBase::is_ascii_digit);
        }

        parsed_version.normalize();

        auto d1 = Strings::strto<int>(parsed_version.major);
        if (!d1.has_value()) return {};

        auto d2 = Strings::strto<int>(parsed_version.minor);
        if (!d2.has_value()) return {};

        auto d3 = Strings::strto<int>(parsed_version.patch);
        if (!d3.has_value()) return {};

        return std::array<int, 3>{*d1.get(), *d2.get(), *d3.get()};
    }

    Optional<ToolOs> to_tool_os(StringView os) noexcept
    {
        for (auto&& entry : all_tool_oses)
        {
            if (os == entry.name)
            {
                return entry.os;
            }
        }

        return nullopt;
    }

    StringLiteral to_string_literal(ToolOs os) noexcept
    {
        for (auto&& entry : all_tool_oses)
        {
            if (os == entry.os)
            {
                return entry.name;
            }
        }

        Checks::unreachable(VCPKG_LINE_INFO, "Unexpected ToolOs");
    }

    LocalizedString all_comma_separated_tool_oses()
    {
        return LocalizedString::from_raw(
            Strings::join(", ", all_tool_oses, [](const ToolOsEntry& entry) { return entry.name; }));
    }

    Optional<std::vector<ToolDataEntry>> parse_tool_data(DiagnosticContext& context,
                                                         StringView contents,
                                                         StringView origin)
    {
        return Json::parse_object(context, contents, origin)
            .then([&](Json::Object&& as_object) -> Optional<std::vector<ToolDataEntry>> {
                Json::Reader r(origin);
                auto maybe_tool_data = ToolDataFileDeserializer::instance.visit(r, as_object);
                if (!r.messages().good())
                {
                    r.messages().report(context);
                    return nullopt;
                }

                return maybe_tool_data.value_or_exit(VCPKG_LINE_INFO);
            });
    }

    static Optional<std::vector<ToolDataEntry>> parse_tool_data_file(DiagnosticContext& context,
                                                                     const ReadOnlyFilesystem& fs,
                                                                     Path path)
    {
        return fs.try_read_contents(context, path)
            .then([&](FileContents&& contents) -> Optional<std::vector<ToolDataEntry>> {
                return parse_tool_data(context, contents.content, contents.origin);
            });
    }

    const ToolDataEntry* get_raw_tool_data(const std::vector<ToolDataEntry>& tool_data_table,
                                           StringView toolname,
                                           const CPUArchitecture arch,
                                           const ToolOs os)
    {
        const ToolDataEntry* default_tool = nullptr;
        for (auto&& tool_candidate : tool_data_table)
        {
            if (tool_candidate.tool == toolname && tool_candidate.os == os)
            {
                if (!tool_candidate.arch)
                {
                    if (!default_tool)
                    {
                        default_tool = &tool_candidate;
                    }
                }
                else if (arch == *tool_candidate.arch.get())
                {
                    return &tool_candidate;
                }
            }
        }
        return default_tool;
    }

    static Optional<ToolData> get_tool_data(const std::vector<ToolDataEntry>& tool_data_table, StringView tool)
    {
        auto hp = get_host_processor();
#if defined(_WIN32)
        auto data = get_raw_tool_data(tool_data_table, tool, hp, ToolOs::Windows);
#elif defined(__APPLE__)
        auto data = get_raw_tool_data(tool_data_table, tool, hp, ToolOs::Osx);
#elif defined(__linux__)
        auto data = get_raw_tool_data(tool_data_table, tool, hp, ToolOs::Linux);
#elif defined(__FreeBSD__)
        auto data = get_raw_tool_data(tool_data_table, tool, hp, ToolOs::FreeBsd);
#elif defined(__OpenBSD__)
        auto data = get_raw_tool_data(tool_data_table, tool, hp, ToolOs::OpenBsd);
#elif defined(__NetBSD__)
        auto data = get_raw_tool_data(tool_data_table, tool, hp, ToolOs::NetBsd);
#elif defined(__SVR4) && defined(__sun)
        auto data = get_raw_tool_data(tool_data_table, tool, hp, ToolOs::Solaris);
#else
        ToolDataEntry* data = nullptr;
#endif
        if (!data)
        {
            return nullopt;
        }

        Path tool_dir_name = fmt::format("{}-{}-{}", tool, data->version.raw, data->os);
        Path download_subpath;
        if (!data->archiveName.empty())
        {
            download_subpath = data->archiveName;
        }
        else if (!data->exeRelativePath.empty())
        {
            download_subpath = Strings::concat(StringView{data->sha512}.substr(0, 8), '-', data->exeRelativePath);
        }

        return ToolData{tool.to_string(),
                        data->version.cooked,
                        data->exeRelativePath,
                        data->url,
                        download_subpath,
                        !data->archiveName.empty(),
                        tool_dir_name,
                        data->sha512};
    }

    static bool download_is_available(const Optional<ToolData>& tool_data) noexcept
    {
        if (auto td = tool_data.get())
        {
            return !td->url.empty();
        }

        return false;
    }

    struct PathAndVersion
    {
        Path p;
        std::string version;
    };

    static Optional<std::string> run_to_extract_version(DiagnosticContext& context,
                                                        StringLiteral tool_name,
                                                        const Path& exe_path,
                                                        Command&& cmd)
    {
        auto maybe_exit_code_and_output = cmd_execute_and_capture_output(context, cmd);
        if (auto output = check_zero_exit_code(context, cmd, maybe_exit_code_and_output))
        {
            return std::move(*output);
        }

        context.report(DiagnosticLine{
            DiagKind::Note, exe_path, msg::format(msgWhileDeterminingVersion, msg::tool_name = tool_name)});
        return nullopt;
    }

    // set target to the subrange [begin_idx, end_idx)
    static void set_string_to_subrange(std::string& target, size_t begin_idx, size_t end_idx)
    {
        if (end_idx != std::string::npos)
        {
            target.resize(end_idx);
        }
        target.erase(0, begin_idx);
    }

    static void report_unexpected_tool_output(DiagnosticContext& context,
                                              StringLiteral tool_name,
                                              Optional<std::string>& maybe_output,
                                              const Path& exe_path)
    {
        auto diag = msg::format(msgUnexpectedToolOutput2, msg::tool_name = tool_name);
        if (auto* output = maybe_output.get())
        {
            diag.append_raw('\n').append_raw(std::move(*output));
            maybe_output.clear();
        }

        context.report(DiagnosticLine{DiagKind::Error, exe_path, std::move(diag)});
    }

    void extract_prefixed_nonquote(DiagnosticContext& context,
                                   StringLiteral prefix,
                                   StringLiteral tool_name,
                                   Optional<std::string>& maybe_output,
                                   const Path& exe_path)
    {
        if (auto* output = maybe_output.get())
        {
            auto idx = output->find(prefix.data(), 0, prefix.size());
            if (idx != std::string::npos)
            {
                idx += prefix.size();
                const auto end_idx = output->find('"', idx);
                set_string_to_subrange(*output, idx, end_idx);
                return;
            }

            report_unexpected_tool_output(context, tool_name, maybe_output, exe_path);
        }
    }

    void extract_prefixed_nonwhitespace(DiagnosticContext& context,
                                        StringLiteral prefix,
                                        StringLiteral tool_name,
                                        Optional<std::string>& maybe_output,
                                        const Path& exe_path)
    {
        if (auto* output = maybe_output.get())
        {
            auto idx = output->find(prefix.data(), 0, prefix.size());
            if (idx != std::string::npos)
            {
                idx += prefix.size();
                const auto end_idx = output->find_first_of(" \r\n", idx, 3);
                set_string_to_subrange(*output, idx, end_idx);
                return;
            }

            report_unexpected_tool_output(context, tool_name, maybe_output, exe_path);
        }
    }

    struct ToolProvider
    {
        virtual StringView tool_data_name() const = 0;
        /// \returns The stem of the executable to search PATH for, or empty string if tool can't be searched
        virtual std::vector<StringView> system_exe_stems() const { return std::vector<StringView>{}; }
        virtual std::array<int, 3> default_min_version() const = 0;
        /// \returns \c true if the tool's version is included in package ABI calculations. ABI sensitive tools will be
        /// pinned to exact versions if \c --x-abi-tools-use-exact-versions is passed.
        virtual bool is_abi_sensitive() const = 0;
        /// \returns \c true if and only if it is impossible to retrieve the tool's version, and thus it should not be
        // considered.
        virtual bool ignore_version() const { return false; }

        virtual void add_system_paths(DiagnosticContext& context,
                                      const ReadOnlyFilesystem& fs,
                                      std::vector<Path>& out_candidate_paths) const
        {
            (void)context;
            (void)fs;
            (void)out_candidate_paths;
        }

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem& fs,
                                                  const ToolCache& cache,
                                                  const Path& exe_path) const = 0;

        // returns true if and only if `exe_path` is a usable version of this tool, cheap check
        virtual bool cheap_is_acceptable(const Path& exe_path) const
        {
            (void)exe_path;
            return true;
        }

        // returns true if and only if `exe_path` is a usable version of this tool
        virtual bool is_acceptable(const Path& exe_path) const
        {
            (void)exe_path;
            return true;
        }

        virtual void add_system_package_info(LocalizedString& out) const
        {
            out.append_raw(' ').append(msgInstallWithSystemManager);
        }
    };

    struct GenericToolProvider : ToolProvider
    {
        explicit GenericToolProvider(StringView tool) : m_tool_data_name(tool) { }

        const StringView m_tool_data_name;

        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return m_tool_data_name; }
        virtual std::array<int, 3> default_min_version() const override { return {0}; }
        virtual bool ignore_version() const override { return true; }

        virtual Optional<std::string> get_version(DiagnosticContext&,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path&) const override
        {
            return {"0"};
        }
    };

    struct AzCopyProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return "azcopy"; }
        virtual std::vector<StringView> system_exe_stems() const override { return {"azcopy"}; }
        virtual std::array<int, 3> default_min_version() const override { return {10, 29, 1}; }

#if defined(_WIN32)
        virtual void add_system_paths(DiagnosticContext& context,
                                      const ReadOnlyFilesystem&,
                                      std::vector<Path>& out_candidate_paths) const override
        {
            if (const auto appdata_local = get_appdata_local(context))
            {
                // as installed by WinGet
                out_candidate_paths.push_back(*appdata_local / "Microsoft\\WinGet\\Links\\azcopy.exe");
            }

            // other common installation locations
            if (auto system_drive = get_system_drive(context))
            {
                // https://devdiv.visualstudio.com/XlabImageFactory/_git/XlabImageFactory?path=/artifacts/windows-azcopy-downloadfile/windows-azcopy-downloadfile.ps1&version=GBmain&_a=contents&line=54&lineStyle=plain&lineEnd=55&lineStartColumn=1&lineEndColumn=1
                out_candidate_paths.emplace_back(*system_drive / "\\AzCopy10\\azcopy.exe");
                // https://devdiv.visualstudio.com/XlabImageFactory/_git/XlabImageFactory?path=/artifacts/windows-AZCopy10/windows-AzCopy10.ps1&version=GBmain&_a=contents&line=8&lineStyle=plain&lineEnd=8&lineStartColumn=1&lineEndColumn=79
                out_candidate_paths.emplace_back(*system_drive / "\\AzCopy10\\AZCopy\\azcopy.exe");
            }
        }
#endif

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // azcopy --version outputs e.g. "azcopy version 10.13.0"
            auto maybe_output =
                run_to_extract_version(context, "azcopy", exe_path, Command(exe_path).string_arg("--version"));
            extract_prefixed_nonwhitespace(context, "azcopy version ", "azcopy", maybe_output, exe_path);
            return maybe_output;
        }
    };

    struct CMakeProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return true; }
        virtual StringView tool_data_name() const override { return Tools::CMAKE; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::CMAKE}; }
        virtual std::array<int, 3> default_min_version() const override { return {3, 17, 1}; }

#if defined(_WIN32)
        virtual void add_system_paths(DiagnosticContext&,
                                      const ReadOnlyFilesystem&,
                                      std::vector<Path>& out_candidate_paths) const override
        {
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get())
            {
                out_candidate_paths.push_back(*pf / "CMake" / "bin" / "cmake.exe");
            }

            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get())
            {
                out_candidate_paths.push_back(*pf / "CMake" / "bin" / "cmake.exe");
            }
        }

        virtual bool cheap_is_acceptable(const Path& exe_path) const override
        {
            // the cmake version from mysys and cygwin can not be used because that version can't handle 'C:' in paths
            auto path = exe_path.generic_u8string();
            return !Strings::ends_with(path, "/usr/bin") && !Strings::ends_with(path, "/cygwin64/bin");
        }

#endif
        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // Sample output:
            // cmake version 3.10.2
            //
            // CMake suite maintained and supported by Kitware (kitware.com/cmake).

            // There are two expected output formats to handle: "cmake3 version x.x.x" and "cmake version x.x.x"
            auto maybe_output =
                run_to_extract_version(context, Tools::CMAKE, exe_path, Command(exe_path).string_arg("--version"));
            if (auto* output = maybe_output.get())
            {
                Strings::inplace_replace_all(*output, "cmake3", "cmake");
                extract_prefixed_nonwhitespace(context, "cmake version ", Tools::CMAKE, maybe_output, exe_path);
            }

            return maybe_output;
        }
    };

    struct NinjaProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::NINJA; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::NINJA}; }
        virtual std::array<int, 3> default_min_version() const override { return {3, 5, 1}; }
#if !defined(_WIN32)
        virtual void add_system_paths(DiagnosticContext&,
                                      const ReadOnlyFilesystem&,
                                      std::vector<Path>& out_candidate_paths) const override
        {
            // This is where Ninja goes by default on Alpine: https://github.com/microsoft/vcpkg/issues/21218
            out_candidate_paths.emplace_back("/usr/lib/ninja-build/bin");
        }
#endif // ^^^ !defined(_WIN32)

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // Sample output: 1.8.2
            return run_to_extract_version(context, Tools::NINJA, exe_path, Command(exe_path).string_arg("--version"));
        }
    };

    struct NuGetProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::NUGET; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::NUGET}; }
        virtual std::array<int, 3> default_min_version() const override { return {4, 6, 2}; }

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem& fs,
                                                  const ToolCache& cache,
                                                  const Path& exe_path) const override
        {
            Command cmd;
#if defined(_WIN32)
            (void)fs;
            (void)cache;
#else
            const auto* mono_path = cache.get_tool_path(context, fs, Tools::MONO);
            if (!mono_path)
            {
                context.report(DiagnosticLine{DiagKind::Note, msg::format(msgMonoInstructions)});
                return nullopt;
            }

            cmd.string_arg(*mono_path);
#endif // ^^^ !_WIN32
            cmd.string_arg(exe_path).string_arg("help").string_arg("-ForceEnglishOutput");
            auto maybe_output = run_to_extract_version(context, Tools::NUGET, exe_path, std::move(cmd));
            if (maybe_output)
            {
                // Sample output:
                // NuGet Version: 4.6.2.5055
                // usage: NuGet <command> [args] [options]
                // Type 'NuGet help <command>' for help on a specific command.
                extract_prefixed_nonwhitespace(context, "NuGet Version: ", Tools::NUGET, maybe_output, exe_path);
            }
#if !defined(_WIN32)
            else
            {
                context.report(DiagnosticLine{DiagKind::Note, msg::format(msgMonoInstructions)});
            }
#endif // ^^^ !_WIN32

            return maybe_output;
        }
    };

    struct NodeProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::NODE; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::NODE}; }
        virtual std::array<int, 3> default_min_version() const override { return {16, 12, 0}; }

#if defined(_WIN32)
        virtual void add_system_paths(DiagnosticContext&,
                                      const ReadOnlyFilesystem&,
                                      std::vector<Path>& out_candidate_paths) const override
        {
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get()) out_candidate_paths.push_back(*pf / "nodejs" / "node.exe");
            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get()) out_candidate_paths.push_back(*pf / "nodejs" / "node.exe");
        }
#endif

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // Sample output: v16.12.0
            auto maybe_output =
                run_to_extract_version(context, Tools::NODE, exe_path, Command(exe_path).string_arg("--version"));
            extract_prefixed_nonwhitespace(context, "v", Tools::NODE, maybe_output, exe_path);
            return maybe_output;
        }
    };

    struct GitProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::GIT; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::GIT}; }
        virtual std::array<int, 3> default_min_version() const override { return {2, 7, 4}; }

#if defined(_WIN32)
        virtual void add_system_paths(DiagnosticContext&,
                                      const ReadOnlyFilesystem&,
                                      std::vector<Path>& out_candidate_paths) const override
        {
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get()) out_candidate_paths.push_back(*pf / "git" / "cmd" / "git.exe");
            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get())
            {
                out_candidate_paths.push_back(*pf / "git" / "cmd" / "git.exe");
            }
        }
#endif

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // Sample output: git version 2.17.1.windows.2
            auto maybe_output =
                run_to_extract_version(context, Tools::GIT, exe_path, Command(exe_path).string_arg("--version"));
            extract_prefixed_nonwhitespace(context, "git version ", Tools::GIT, maybe_output, exe_path);
            return maybe_output;
        }
    };

    struct MonoProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::MONO; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::MONO}; }
        virtual std::array<int, 3> default_min_version() const override { return {0, 0, 0}; }

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // Sample output:
            // Mono JIT compiler version 6.8.0.105 (Debian 6.8.0.105+dfsg-2 Wed Feb 26 23:23:50 UTC 2020)
            auto maybe_output =
                run_to_extract_version(context, Tools::MONO, exe_path, Command(exe_path).string_arg("--version"));
            extract_prefixed_nonwhitespace(context, "Mono JIT compiler version ", Tools::MONO, maybe_output, exe_path);
            return maybe_output;
        }

        virtual void add_system_package_info(LocalizedString& out) const override
        {
#if defined(__APPLE__)
            out.append_raw(' ').append(msgInstallWithSystemManagerPkg, msg::command_line = "brew install mono");
#else
            out.append_raw(' ').append(msgInstallWithSystemManagerPkg,
                                       msg::command_line = "sudo apt install mono-complete");
            out.append_raw(' ').append(msgInstallWithSystemManagerMono,
                                       msg::url = "https://www.mono-project.com/download/stable/");
#endif
        }
    };

    struct GsutilProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::GSUTIL; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::GSUTIL}; }
        virtual std::array<int, 3> default_min_version() const override { return {4, 56, 0}; }

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // Sample output: gsutil version: 4.58
            auto maybe_output =
                run_to_extract_version(context, Tools::GSUTIL, exe_path, Command(exe_path).string_arg("version"));
            extract_prefixed_nonwhitespace(context, "gsutil version: ", Tools::GSUTIL, maybe_output, exe_path);
            return maybe_output;
        }
    };

    struct AwsCliProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::AWSCLI; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::AWSCLI}; }
        virtual std::array<int, 3> default_min_version() const override { return {2, 4, 4}; }

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // Sample output: aws-cli/2.4.4 Python/3.8.8 Windows/10 exe/AMD64 prompt/off
            auto maybe_output =
                run_to_extract_version(context, Tools::AWSCLI, exe_path, Command(exe_path).string_arg("--version"));
            extract_prefixed_nonwhitespace(context, "aws-cli/", Tools::AWSCLI, maybe_output, exe_path);
            return maybe_output;
        }
    };

    struct AzCliProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::AZCLI; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::AZCLI}; }
        virtual std::array<int, 3> default_min_version() const override { return {2, 64, 0}; }

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // {
            //    ...
            //   "azure-cli": "2.64.0",
            //    ...
            // }
            auto maybe_output = run_to_extract_version(
                context,
                Tools::AZCLI,
                exe_path,
                Command(exe_path).string_arg("version").string_arg("--output").string_arg("json"));
            extract_prefixed_nonquote(context, "\"azure-cli\": \"", Tools::AZCLI, maybe_output, exe_path);
            return maybe_output;
        }
    };

    struct CosCliProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::COSCLI; }
        virtual std::vector<StringView> system_exe_stems() const override { return {"cos"}; }
        virtual std::array<int, 3> default_min_version() const override { return {0, 11, 0}; }

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // Sample output: coscli version v0.11.0-beta
            auto maybe_output =
                run_to_extract_version(context, Tools::COSCLI, exe_path, Command(exe_path).string_arg("--version"));
            extract_prefixed_nonwhitespace(context, "coscli version v", Tools::COSCLI, maybe_output, exe_path);
            return maybe_output;
        }
    };

    struct PowerShellCoreProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override
        {
            // This #ifdef is mirrored in build.cpp's compute_abi_tag
#if defined(_WIN32)
            return true;
#else
            return false;
#endif
        }
        virtual StringView tool_data_name() const override { return Tools::POWERSHELL_CORE; }
        virtual std::vector<StringView> system_exe_stems() const override { return {"pwsh"}; }
        virtual std::array<int, 3> default_min_version() const override { return {7, 0, 3}; }

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // Sample output: PowerShell 7.0.3\r\n
            auto maybe_output = run_to_extract_version(
                context, Tools::POWERSHELL_CORE, exe_path, Command(exe_path).string_arg("--version"));
            extract_prefixed_nonwhitespace(context, "PowerShell ", Tools::POWERSHELL_CORE, maybe_output, exe_path);
            return maybe_output;
        }
    };

    struct Python3Provider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::PYTHON3; }
        virtual std::vector<StringView> system_exe_stems() const override { return {"python3", "py3", "python", "py"}; }
        virtual std::array<int, 3> default_min_version() const override { return {3, 5, 0}; } // 3.5 added -m venv

#if defined(_WIN32)
        void add_system_paths_impl(DiagnosticContext& context,
                                   const ReadOnlyFilesystem& fs,
                                   std::vector<Path>& out_candidate_paths,
                                   const Path& program_files_root) const
        {
            auto maybe_candidates = fs.try_get_directories_non_recursive(context, program_files_root);
            if (auto candidates = maybe_candidates.get())
            {
                for (auto&& candidate : *candidates)
                {
                    auto name = candidate.filename();
                    if (Strings::case_insensitive_ascii_starts_with(name, "Python") &&
                        std::all_of(name.begin() + 6, name.end(), ParserBase::is_ascii_digit))
                    {
                        out_candidate_paths.emplace_back(std::move(candidate));
                    }
                }
            }
        }

        virtual void add_system_paths(DiagnosticContext& context,
                                      const ReadOnlyFilesystem& fs,
                                      std::vector<Path>& out_candidate_paths) const override
        {
            if (const auto pf = get_program_files_platform_bitness().get())
            {
                add_system_paths_impl(context, fs, out_candidate_paths, *pf);
            }

            if (const auto pf = get_program_files_32_bit().get())
            {
                add_system_paths_impl(context, fs, out_candidate_paths, *pf);
            }
        }
#endif // ^^^ _WIN32

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // Sample output: Python 3.10.2\r\n
            auto maybe_output =
                run_to_extract_version(context, Tools::PYTHON3, exe_path, Command(exe_path).string_arg("--version"));
            extract_prefixed_nonwhitespace(context, "Python ", Tools::PYTHON3, maybe_output, exe_path);
            return maybe_output;
        }

        virtual void add_system_package_info(LocalizedString& out) const override
        {
#if defined(__APPLE__)
            out.append_raw(' ').append(msgInstallWithSystemManagerPkg, msg::command_line = "brew install python3");
#else
            out.append_raw(' ').append(msgInstallWithSystemManagerPkg, msg::command_line = "sudo apt install python3");
#endif
        }
    };

    struct Python3WithVEnvProvider : Python3Provider
    {
        virtual StringView tool_data_name() const override { return Tools::PYTHON3_WITH_VENV; }

        virtual bool is_acceptable(const Path& exe_path) const override
        {
            return flatten(cmd_execute_and_capture_output(
                               {Command(exe_path).string_arg("-m").string_arg("venv").string_arg("-h")}),
                           Tools::PYTHON3)
                .has_value();
        }

        virtual void add_system_package_info(LocalizedString& out) const override
        {
#if defined(__APPLE__)
            out.append_raw(' ').append(msgInstallWithSystemManagerPkg, msg::command_line = "brew install python3");
#else
            out.append_raw(' ').append(msgInstallWithSystemManagerPkg,
                                       msg::command_line = "sudo apt install python3-virtualenv");
#endif
        }
    };

    struct SevenZipProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::SEVEN_ZIP; }
        virtual std::vector<StringView> system_exe_stems() const override { return {"7z"}; }
        virtual std::array<int, 3> default_min_version() const override { return {24, 9}; }

#if defined(_WIN32)
        virtual void add_system_paths(DiagnosticContext&,
                                      const ReadOnlyFilesystem&,
                                      std::vector<Path>& out_candidate_paths) const override
        {
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get())
            {
                out_candidate_paths.push_back(*pf / "7-Zip" / "7z.exe");
            }

            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get())
            {
                out_candidate_paths.push_back(*pf / "7-Zip" / "7z.exe");
            }
        }
#endif

        virtual Optional<std::string> get_version(DiagnosticContext& context,
                                                  const Filesystem&,
                                                  const ToolCache&,
                                                  const Path& exe_path) const override
        {
            // Sample output: 7-Zip 24.09 (x64) : Copyright (c) 1999-2024 Igor Pavlov : 2024-11-29
            auto maybe_output = run_to_extract_version(context, Tools::SEVEN_ZIP, exe_path, Command(exe_path));
            extract_prefixed_nonwhitespace(context, "7-Zip ", Tools::SEVEN_ZIP, maybe_output, exe_path);
            return maybe_output;
        }
    };

    struct ToolCacheImpl final : ToolCache
    {
        AssetCachingSettings asset_cache_settings;
        const Path downloads;
        const Path config_path;
        const Path tools;
        const RequireExactVersions abiToolVersionHandling;

        ContextCache<std::string, PathAndVersion> path_version_cache;
        mutable Optional<ExpectedT<std::vector<ToolDataEntry>, std::vector<DiagnosticLine>>> m_tool_data_cache;

        ToolCacheImpl(const AssetCachingSettings& asset_cache_settings,
                      Path downloads,
                      Path config_path,
                      Path tools,
                      RequireExactVersions abiToolVersionHandling)
            : asset_cache_settings(asset_cache_settings)
            , downloads(std::move(downloads))
            , config_path(std::move(config_path))
            , tools(std::move(tools))
            , abiToolVersionHandling(abiToolVersionHandling)
        {
        }

        /**
         * @param accept_version Callback that accepts a std::array<int,3> and returns true if the version is accepted
         * @param log_candidate Callback that accepts Path, ExpectedL<std::string> maybe_version. Gets called on every
         * existing candidate.
         */
        template<typename Func, typename Func2>
        Optional<PathAndVersion> find_first_with_sufficient_version(DiagnosticContext& context,
                                                                    const Filesystem& fs,
                                                                    const ToolProvider& tool_provider,
                                                                    const std::vector<Path>& candidates,
                                                                    Func&& accept_version,
                                                                    const Func2& log_candidate) const
        {
            for (auto&& candidate : candidates)
            {
                if (!fs.exists(candidate, IgnoreErrors{})) continue;
                if (!tool_provider.cheap_is_acceptable(candidate)) continue;
                AttemptDiagnosticContext adc{context};
                auto maybe_version = tool_provider.get_version(adc, fs, *this, candidate);
                const auto version = maybe_version.get();
                if (!version)
                {
                    log_candidate(candidate, adc.to_string());
                    adc.handle();
                    continue;
                }

                log_candidate(candidate, *version);
                const auto parsed_version = parse_tool_version_string(*version);
                if (!parsed_version) continue;
                auto& actual_version = *parsed_version.get();
                if (!accept_version(actual_version)) continue;
                if (!tool_provider.is_acceptable(candidate)) continue;
                adc.commit();

                return PathAndVersion{candidate, *version};
            }

            return nullopt;
        }

        Optional<Path> download_tool(DiagnosticContext& context, const Filesystem& fs, const ToolData& tool_data) const
        {
            using namespace Hash;

            const std::array<int, 3>& version = tool_data.version;
            const std::string version_as_string = fmt::format("{}.{}.{}", version[0], version[1], version[2]);
            if (tool_data.url.empty())
            {
                context.report(DiagnosticLine{DiagKind::Error,
                                              config_path,
                                              msg::format(msgToolOfVersionXNotFound,
                                                          msg::tool_name = tool_data.name,
                                                          msg::version = version_as_string)});
                return nullopt;
            }

            context.statusln(msg::format(
                msgDownloadingPortableToolVersionX, msg::tool_name = tool_data.name, msg::version = version_as_string));
            const auto download_path = downloads / tool_data.download_subpath;
            const auto hash_result = get_file_hash(context, fs, download_path, Algorithm::Sha512);
            switch (hash_result.prognosis)
            {
                case HashPrognosis::Success:
                    if (!Strings::case_insensitive_ascii_equals(tool_data.sha512, hash_result.hash))
                    {
                        context.report(DiagnosticLine{DiagKind::Error,
                                                      download_path,
                                                      msg::format(msgToolHashMismatch,
                                                                  msg::tool_name = tool_data.name,
                                                                  msg::expected = tool_data.sha512,
                                                                  msg::actual = hash_result.hash)});
                        return nullopt;
                    }

                    break;
                case HashPrognosis::FileNotFound:
                    if (!download_file_asset_cached(context,
                                                    null_sink,
                                                    asset_cache_settings,
                                                    fs,
                                                    tool_data.url,
                                                    {},
                                                    download_path,
                                                    tool_data.download_subpath,
                                                    tool_data.sha512))
                    {
                        return nullopt;
                    }
                    break;
                case HashPrognosis::OtherError: return nullopt;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }

            const auto tool_dir_path = tools / tool_data.tool_dir_subpath;
            Path exe_path = tool_dir_path / tool_data.exe_subpath;

            if (tool_data.is_archive)
            {
                context.statusln(msg::format(msgExtractingTool, msg::tool_name = tool_data.name));
                auto maybe_partial_path =
                    extract_archive_to_temp_subdirectory(context, fs, *this, download_path, tool_dir_path);
                if (auto partial_path = maybe_partial_path.get())
                {
                    if (!fs.rename_or_delete(context, *partial_path, tool_dir_path))
                    {
                        return nullopt;
                    }

                    if (!fs.exists(exe_path, IgnoreErrors{}))
                    {
                        context.report(DiagnosticLine{
                            DiagKind::Error,
                            exe_path,
                            msg::format(msgExpectedPathToExistAfterExtractingTool, msg::tool_name = tool_data.name)});
                        context.report(DiagnosticLine{DiagKind::Note, tool_dir_path, msg::format(msgExtractedHere)});
                        return nullopt;
                    }
                }
                else
                {
                    return nullopt;
                }
            }
            else if (!fs.create_directories(context, exe_path.parent_path()) ||
                     !fs.rename_or_delete(context, download_path, exe_path))
            {
                return nullopt;
            }

            return exe_path;
        }

        virtual const Path* get_tool_path(DiagnosticContext& context,
                                          const Filesystem& fs,
                                          StringView tool) const override
        {
            if (auto p = get_tool_pathversion(context, fs, tool))
            {
                return &p->p;
            }

            return nullptr;
        }

        Optional<PathAndVersion> get_path(DiagnosticContext& context,
                                          const Filesystem& fs,
                                          const ToolProvider& tool) const
        {
            const bool env_force_system_binaries =
                get_environment_variable(EnvironmentVariableVcpkgForceSystemBinaries).has_value();
            const bool env_force_download_binaries =
                get_environment_variable(EnvironmentVariableVcpkgForceDownloadedBinaries).has_value();
            auto maybe_all_tool_data = load_tool_data(context, fs);
            if (!maybe_all_tool_data)
            {
                return nullopt;
            }

            const auto maybe_tool_data = get_tool_data(*maybe_all_tool_data, tool.tool_data_name());
            const bool download_available = download_is_available(maybe_tool_data);
            // search for system searchable tools unless forcing downloads and download available
            const auto system_exe_stems = tool.system_exe_stems();
            const bool consider_system =
                !system_exe_stems.empty() && !(env_force_download_binaries && download_available);
            // search for downloaded tools unless forcing system search
            const bool consider_downloads = !env_force_system_binaries || !consider_system;

            const bool exact_version = tool.is_abi_sensitive() && abiToolVersionHandling == RequireExactVersions::YES;
            // forcing system search also disables version detection
            const bool ignore_version = env_force_system_binaries || tool.ignore_version();

            std::vector<Path> candidate_paths;
            std::array<int, 3> min_version = tool.default_min_version();

            if (auto tool_data = maybe_tool_data.get())
            {
                // If there is an entry for the tool in vcpkg-tools.json, use that version as the minimum
                min_version = tool_data->version;

                if (consider_downloads)
                {
                    // If we would consider downloading the tool, prefer the downloaded copy
                    candidate_paths.push_back(tool_data->exe_path(tools));
                }
            }

            if (consider_system)
            {
                // If we are considering system copies, first search the PATH, then search any special system locations
                // (e.g Program Files).
                auto paths_from_path = fs.find_from_PATH(system_exe_stems);
                candidate_paths.insert(candidate_paths.end(), paths_from_path.cbegin(), paths_from_path.cend());
                tool.add_system_paths(context, fs, candidate_paths);
            }

            std::string considered_versions;
            if (ignore_version)
            {
                // If we are forcing the system copy (and therefore ignoring versions), take the first entry that
                // is acceptable.
                const auto it =
                    std::find_if(candidate_paths.begin(), candidate_paths.end(), [&fs, &tool](const Path& p) {
                        return fs.is_regular_file(p) && tool.is_acceptable(p);
                    });

                if (it != candidate_paths.end())
                {
                    return PathAndVersion{std::move(*it), "0"};
                }
            }
            else
            {
                // Otherwise, execute each entry and compare its version against the constraint. Take the first that
                // matches.
                const auto maybe_path = find_first_with_sufficient_version(
                    context,
                    fs,
                    tool,
                    candidate_paths,
                    [&min_version, exact_version](const std::array<int, 3>& actual_version) {
                        if (exact_version)
                        {
                            return actual_version[0] == min_version[0] && actual_version[1] == min_version[1] &&
                                   actual_version[2] == min_version[2];
                        }
                        return actual_version[0] > min_version[0] ||
                               (actual_version[0] == min_version[0] && actual_version[1] > min_version[1]) ||
                               (actual_version[0] == min_version[0] && actual_version[1] == min_version[1] &&
                                actual_version[2] >= min_version[2]);
                    },
                    [&](const Path& path, const std::string& version) {
                        considered_versions += fmt::format("{}: {}\n", path, version);
                    });
                if (const auto p = maybe_path.get())
                {
                    return *p;
                }
            }

            if (consider_downloads)
            {
                // If none of the current entries are acceptable, fall back to downloading if possible
                if (auto tool_data = maybe_tool_data.get())
                {
                    auto maybe_downloaded_path = download_tool(context, fs, *tool_data);
                    if (auto downloaded_path = maybe_downloaded_path.get())
                    {
                        auto maybe_downloaded_version = tool.get_version(context, fs, *this, *downloaded_path);
                        if (auto downloaded_version = maybe_downloaded_version.get())
                        {
                            return PathAndVersion{std::move(*downloaded_path), std::move(*downloaded_version)};
                        }
                        else
                        {
                            return nullopt;
                        }
                    }
                    else
                    {
                        return nullopt;
                    }
                }
            }

            // If no acceptable tool was found and downloading was unavailable, emit an error message
            LocalizedString s = msg::format_error(msgToolFetchFailed, msg::tool_name = tool.tool_data_name());
            if (env_force_system_binaries && download_available)
            {
                s.append_raw(' ').append(msgDownloadAvailable,
                                         msg::env_var =
                                             format_environment_variable(EnvironmentVariableVcpkgForceSystemBinaries));
            }
            if (consider_system)
            {
                tool.add_system_package_info(s);
            }
            else if (!download_available)
            {
                s.append_raw(' ').append(msgUnknownTool);
            }
            if (!considered_versions.empty())
            {
                s.append_raw('\n')
                    .append(msgConsideredVersions, msg::version = fmt::join(min_version, "."))
                    .append_raw('\n')
                    .append_raw(considered_versions);
            }

            context.report_error(std::move(s));
            return nullopt;
        }

        const PathAndVersion* get_tool_pathversion(DiagnosticContext& context,
                                                   const Filesystem& fs,
                                                   StringView tool) const
        {
            return path_version_cache.get_lazy(
                context, tool, [this, &fs, &tool](DiagnosticContext& inner_context) -> Optional<PathAndVersion> {
                    // First deal with specially handled tools.
                    // For these we may look in locations like Program Files, the PATH etc as well as the
                    // auto-downloaded location.
                    if (tool == Tools::CMAKE) return get_path(inner_context, fs, CMakeProvider());
                    if (tool == Tools::GIT) return get_path(inner_context, fs, GitProvider());
                    if (tool == Tools::NINJA) return get_path(inner_context, fs, NinjaProvider());
                    if (tool == Tools::POWERSHELL_CORE) return get_path(inner_context, fs, PowerShellCoreProvider());
                    if (tool == Tools::NUGET) return get_path(inner_context, fs, NuGetProvider());
                    if (tool == Tools::NODE) return get_path(inner_context, fs, NodeProvider());
                    if (tool == Tools::MONO) return get_path(inner_context, fs, MonoProvider());
                    if (tool == Tools::GSUTIL) return get_path(inner_context, fs, GsutilProvider());
                    if (tool == Tools::AWSCLI) return get_path(inner_context, fs, AwsCliProvider());
                    if (tool == Tools::AZCOPY) return get_path(inner_context, fs, AzCopyProvider());
                    if (tool == Tools::AZCLI) return get_path(inner_context, fs, AzCliProvider());
                    if (tool == Tools::COSCLI) return get_path(inner_context, fs, CosCliProvider());
                    if (tool == Tools::PYTHON3) return get_path(inner_context, fs, Python3Provider());
                    if (tool == Tools::PYTHON3_WITH_VENV)
                    {
                        return get_path(inner_context, fs, Python3WithVEnvProvider());
                    }

                    if (tool == Tools::SEVEN_ZIP || tool == Tools::SEVEN_ZIP_ALT)
                    {
                        return get_path(inner_context, fs, SevenZipProvider());
                    }

                    if (tool == Tools::TAR)
                    {
                        auto maybe_system_tar = find_system_tar(inner_context, fs);
                        if (auto system_tar = maybe_system_tar.get())
                        {
                            return PathAndVersion{std::move(*system_tar), {}};
                        }

                        return nullopt;
                    }

                    if (tool == Tools::CMAKE_SYSTEM)
                    {
                        auto maybe_system_cmake = find_system_cmake(inner_context, fs);
                        if (auto system_cmake = maybe_system_cmake.get())
                        {
                            return PathAndVersion{std::move(*system_cmake), {}};
                        }

                        return nullopt;
                    }

                    GenericToolProvider provider{tool};
                    return get_path(inner_context, fs, provider);
                });
        }

        virtual const std::string* get_tool_version(DiagnosticContext& context,
                                                    const Filesystem& fs,
                                                    StringView tool) const override
        {
            if (auto p = get_tool_pathversion(context, fs, tool))
            {
                return &p->version;
            }

            return nullptr;
        }

        const ExpectedT<std::vector<ToolDataEntry>, std::vector<DiagnosticLine>>& load_tool_data_impl(
            DiagnosticContext& context, const ReadOnlyFilesystem& fs) const
        {
            if (auto existing = m_tool_data_cache.get())
            {
                return *existing;
            }

            ContextBufferedDiagnosticContext cbdc{context};
            auto maybe_tool_data = parse_tool_data_file(cbdc, fs, config_path);
            if (auto success = maybe_tool_data.get())
            {
                return m_tool_data_cache.emplace(std::move(*success));
            }

            return m_tool_data_cache.emplace(std::move(cbdc).lines);
        }

        const std::vector<ToolDataEntry>* load_tool_data(DiagnosticContext& context, const ReadOnlyFilesystem& fs) const
        {
            const auto& loaded_data = load_tool_data_impl(context, fs);
            if (auto success = loaded_data.get())
            {
                return success;
            }

            for (auto&& diag : loaded_data.error())
            {
                context.report(diag);
            }

            return nullptr;
        }
    };

    Optional<Path> find_system_tar(DiagnosticContext& context, const ReadOnlyFilesystem& fs)
    {
#if defined(_WIN32)
        if (auto system32 = get_system32(context))
        {
            auto shipped_with_windows = *system32 / "tar.exe";
            if (fs.is_regular_file(shipped_with_windows))
            {
                return shipped_with_windows;
            }
        }
#endif // ^^^ _WIN32

        const auto tools = fs.find_from_PATH(Tools::TAR);
        if (!tools.empty())
        {
            return tools[0];
        }

        context.report_error(msg::format(msgToolFetchFailed, msg::tool_name = Tools::TAR)
#if defined(_WIN32)
                                 .append(msgToolInWin10)
#else
                                 .append(msgInstallWithSystemManager)
#endif
        );
        return nullopt;
    }

    Optional<Path> find_system_cmake(DiagnosticContext& context, const ReadOnlyFilesystem& fs)
    {
        auto tools = fs.find_from_PATH(Tools::CMAKE);
        if (!tools.empty())
        {
            return std::move(tools[0]);
        }

#if defined(_WIN32)
        std::vector<Path> candidate_paths;
        const auto& program_files = get_program_files_platform_bitness();
        if (const auto pf = program_files.get())
        {
            auto path = *pf / "CMake" / "bin" / "cmake.exe";
            if (fs.exists(path, IgnoreErrors{})) return path;
        }

        const auto& program_files_32_bit = get_program_files_32_bit();
        if (const auto pf = program_files_32_bit.get())
        {
            auto path = *pf / "CMake" / "bin" / "cmake.exe";
            if (fs.exists(path, IgnoreErrors{})) return path;
        }
#endif

        auto error_text = msg::format(msgToolFetchFailed, msg::tool_name = Tools::CMAKE)
#if !defined(_WIN32)
                              .append(msgInstallWithSystemManager)
#endif
            ;

        context.report_error(std::move(error_text));
        return nullopt;
    }

    std::unique_ptr<ToolCache> get_tool_cache(const AssetCachingSettings& asset_cache_settings,
                                              Path downloads,
                                              Path config_path,
                                              Path tools,
                                              RequireExactVersions abiToolVersionHandling)
    {
        return std::make_unique<ToolCacheImpl>(
            asset_cache_settings, downloads, config_path, tools, abiToolVersionHandling);
    }

    struct ToolDataEntryDeserializer final : Json::IDeserializer<ToolDataEntry>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAToolDataObject); }

        virtual View<StringLiteral> valid_fields() const noexcept override
        {
            static const StringLiteral fields[] = {
                JsonIdName,
                JsonIdOS,
                JsonIdVersion,
                JsonIdArch,
                JsonIdExecutable,
                JsonIdUrl,
                JsonIdSha512,
                JsonIdArchive,
            };
            return fields;
        }

        virtual Optional<ToolDataEntry> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            ToolDataEntry value;

            r.required_object_field(
                type_name(), obj, JsonIdName, value.tool, Json::UntypedStringDeserializer::instance);
            r.required_object_field(type_name(), obj, JsonIdOS, value.os, ToolOsDeserializer::instance);
            r.required_object_field(type_name(), obj, JsonIdVersion, value.version, ToolVersionDeserializer::instance);

            r.optional_object_field(obj, JsonIdArch, value.arch, Json::ArchitectureDeserializer::instance);
            r.optional_object_field(
                obj, JsonIdExecutable, value.exeRelativePath, Json::UntypedStringDeserializer::instance);
            r.optional_object_field(obj, JsonIdUrl, value.url, Json::UntypedStringDeserializer::instance);
            r.optional_object_field(obj, JsonIdSha512, value.sha512, Json::Sha512Deserializer::instance);
            r.optional_object_field(obj, JsonIdArchive, value.archiveName, Json::UntypedStringDeserializer::instance);
            return value;
        }

        static const ToolDataEntryDeserializer instance;
    };
    const ToolDataEntryDeserializer ToolDataEntryDeserializer::instance;

    struct ToolDataArrayDeserializer final : Json::ArrayDeserializer<ToolDataEntryDeserializer>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAToolDataArray); }

        static const ToolDataArrayDeserializer instance;
    };
    const ToolDataArrayDeserializer ToolDataArrayDeserializer::instance;

    LocalizedString ToolDataFileDeserializer::type_name() const { return msg::format(msgAToolDataFile); }

    View<StringLiteral> ToolDataFileDeserializer::valid_fields() const noexcept
    {
        static constexpr StringLiteral valid_fields[] = {JsonIdSchemaVersion, JsonIdTools};
        return valid_fields;
    }

    Optional<std::vector<ToolDataEntry>> ToolDataFileDeserializer::visit_object(Json::Reader& r,
                                                                                const Json::Object& obj) const
    {
        int schema_version = -1;
        r.required_object_field(
            type_name(), obj, JsonIdSchemaVersion, schema_version, Json::NaturalNumberDeserializer::instance);

        std::vector<ToolDataEntry> value;
        if (schema_version == 1)
        {
            r.required_object_field(type_name(), obj, JsonIdTools, value, ToolDataArrayDeserializer::instance);
        }
        else
        {
            r.add_generic_error(type_name(),
                                msg::format(msgToolDataFileSchemaVersionNotSupported, msg::version = schema_version));
        }

        return value;
    }

    const ToolDataFileDeserializer ToolDataFileDeserializer::instance;

    LocalizedString ToolOsDeserializer::type_name() const { return msg::format(msgAToolDataOS); }

    Optional<ToolOs> ToolOsDeserializer::visit_string(Json::Reader& r, StringView str) const
    {
        auto maybe_tool_os = to_tool_os(str);
        if (auto tool_os = maybe_tool_os.get())
        {
            return *tool_os;
        }

        r.add_generic_error(
            type_name(),
            msg::format(msgInvalidToolOSValue, msg::value = str, msg::expected = all_comma_separated_tool_oses()));
        return ToolOs::Windows;
    }

    const ToolOsDeserializer ToolOsDeserializer::instance;

    LocalizedString ToolVersionDeserializer::type_name() const { return msg::format(msgAToolDataVersion); }

    Optional<ToolVersion> ToolVersionDeserializer::visit_string(Json::Reader& r, StringView str) const
    {
        auto maybe_parsed = parse_tool_version_string(str);
        if (auto parsed = maybe_parsed.get())
        {
            return ToolVersion{*parsed, str.to_string()};
        }

        r.add_generic_error(type_name(), msg::format(msgInvalidToolVersion));
        return ToolVersion{};
    }

    const ToolVersionDeserializer ToolVersionDeserializer::instance;
}
