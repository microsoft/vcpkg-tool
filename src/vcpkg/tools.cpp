#include <vcpkg/base/cache.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/lazy.h>
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

    ExpectedL<std::vector<ToolDataEntry>> parse_tool_data(StringView contents, StringView origin)
    {
        return Json::parse_object(contents, origin)
            .then([&](Json::Object&& as_object) -> ExpectedL<std::vector<ToolDataEntry>> {
                Json::Reader r(origin);
                auto maybe_tool_data = ToolDataFileDeserializer::instance.visit(r, as_object);
                if (!r.messages().good())
                {
                    return r.messages().join();
                }

                return maybe_tool_data.value_or_exit(VCPKG_LINE_INFO);
            });
    }

    static ExpectedL<std::vector<ToolDataEntry>> parse_tool_data_file(const Filesystem& fs, Path path)
    {
        return fs.try_read_contents(path).then([](FileContents&& fc) -> ExpectedL<std::vector<ToolDataEntry>> {
            return parse_tool_data(fc.content, Path{fc.origin});
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
#elif defined(__SVR4) && defined(__sun)
        auto data = get_raw_tool_data(tool_data_table, tool, hp, ToolOs::Solaris);
#else
        return nullopt;
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

    struct PathAndVersion
    {
        Path p;
        std::string version;
    };

    static ExpectedL<std::string> run_to_extract_version(StringLiteral tool_name, const Path& exe_path, Command&& cmd)
    {
        return flatten_out(cmd_execute_and_capture_output({std::move(cmd)}), exe_path)
            .map_error([&](LocalizedString&& output) {
                return msg::format_error(
                           msgFailedToRunToolToDetermineVersion, msg::tool_name = tool_name, msg::path = exe_path)
                    .append_raw('\n')
                    .append(output);
            });
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

    ExpectedL<std::string> extract_prefixed_nonquote(StringLiteral prefix,
                                                     StringLiteral tool_name,
                                                     std::string&& output,
                                                     const Path& exe_path)
    {
        auto idx = output.find(prefix.data(), 0, prefix.size());
        if (idx != std::string::npos)
        {
            idx += prefix.size();
            const auto end_idx = output.find('"', idx);
            set_string_to_subrange(output, idx, end_idx);
            return {std::move(output), expected_left_tag};
        }

        return std::move(msg::format_error(msgUnexpectedToolOutput, msg::tool_name = tool_name, msg::path = exe_path)
                             .append_raw('\n')
                             .append_raw(std::move(output)));
    }

    ExpectedL<std::string> extract_prefixed_nonwhitespace(StringLiteral prefix,
                                                          StringLiteral tool_name,
                                                          std::string&& output,
                                                          const Path& exe_path)
    {
        auto idx = output.find(prefix.data(), 0, prefix.size());
        if (idx != std::string::npos)
        {
            idx += prefix.size();
            const auto end_idx = output.find_first_of(" \r\n", idx, 3);
            set_string_to_subrange(output, idx, end_idx);
            return {std::move(output), expected_left_tag};
        }

        return std::move(msg::format_error(msgUnexpectedToolOutput, msg::tool_name = tool_name, msg::path = exe_path)
                             .append_raw('\n')
                             .append_raw(std::move(output)));
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

        virtual void add_system_paths(const ReadOnlyFilesystem& fs, std::vector<Path>& out_candidate_paths) const
        {
            (void)fs;
            (void)out_candidate_paths;
        }

        virtual ExpectedL<std::string> get_version(const ToolCache& cache,
                                                   MessageSink& status_sink,
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

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path&) const override
        {
            return {"0", expected_left_tag};
        }
    };

    struct AzCopyProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return "azcopy"; }
        virtual std::vector<StringView> system_exe_stems() const override { return {"azcopy"}; }
        virtual std::array<int, 3> default_min_version() const override { return {10, 29, 1}; }

#if defined(_WIN32)
        virtual void add_system_paths(const ReadOnlyFilesystem&, std::vector<Path>& out_candidate_paths) const override
        {
            const auto& maybe_appdata_local = get_appdata_local();
            if (const auto appdata_local = maybe_appdata_local.get())
            {
                // as installed by WinGet
                out_candidate_paths.push_back(*appdata_local / "Microsoft\\WinGet\\Links\\azcopy.exe");
            }

            // other common installation locations
            const auto& maybe_system_drive = get_system_drive();
            if (auto system_drive = maybe_system_drive.get())
            {
                // https://devdiv.visualstudio.com/XlabImageFactory/_git/XlabImageFactory?path=/artifacts/windows-azcopy-downloadfile/windows-azcopy-downloadfile.ps1&version=GBmain&_a=contents&line=54&lineStyle=plain&lineEnd=55&lineStartColumn=1&lineEndColumn=1
                out_candidate_paths.emplace_back(*system_drive / "\\AzCopy10\\azcopy.exe");
                // https://devdiv.visualstudio.com/XlabImageFactory/_git/XlabImageFactory?path=/artifacts/windows-AZCopy10/windows-AzCopy10.ps1&version=GBmain&_a=contents&line=8&lineStyle=plain&lineEnd=8&lineStartColumn=1&lineEndColumn=79
                out_candidate_paths.emplace_back(*system_drive / "\\AzCopy10\\AZCopy\\azcopy.exe");
            }
        }
#endif

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            // azcopy --version outputs e.g. "azcopy version 10.13.0"
            return run_to_extract_version("azcopy", exe_path, Command(exe_path).string_arg("--version"))
                .then([&](std::string&& output) {
                    return extract_prefixed_nonwhitespace("azcopy version ", "azcopy", std::move(output), exe_path);
                });
        }
    };

    struct CMakeProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return true; }
        virtual StringView tool_data_name() const override { return Tools::CMAKE; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::CMAKE}; }
        virtual std::array<int, 3> default_min_version() const override { return {3, 17, 1}; }

#if defined(_WIN32)
        virtual void add_system_paths(const ReadOnlyFilesystem&, std::vector<Path>& out_candidate_paths) const override
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
        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(Tools::CMAKE, exe_path, Command(exe_path).string_arg("--version"))
                .then([&](std::string&& output) {
                    // Sample output:
                    // cmake version 3.10.2
                    //
                    // CMake suite maintained and supported by Kitware (kitware.com/cmake).

                    // There are two expected output formats to handle: "cmake3 version x.x.x" and "cmake version x.x.x"
                    Strings::inplace_replace_all(output, "cmake3", "cmake");
                    return extract_prefixed_nonwhitespace("cmake version ", Tools::CMAKE, std::move(output), exe_path);
                });
        }
    };

    struct NinjaProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::NINJA; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::NINJA}; }
        virtual std::array<int, 3> default_min_version() const override { return {3, 5, 1}; }
#if !defined(_WIN32)
        virtual void add_system_paths(const ReadOnlyFilesystem&, std::vector<Path>& out_candidate_paths) const override
        {
            // This is where Ninja goes by default on Alpine: https://github.com/microsoft/vcpkg/issues/21218
            out_candidate_paths.emplace_back("/usr/lib/ninja-build/bin");
        }
#endif // ^^^ !defined(_WIN32)

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            // Sample output: 1.8.2
            return run_to_extract_version(Tools::NINJA, exe_path, Command(exe_path).string_arg("--version"));
        }
    };

    struct NuGetProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::NUGET; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::NUGET}; }
        virtual std::array<int, 3> default_min_version() const override { return {4, 6, 2}; }

        virtual ExpectedL<std::string> get_version(const ToolCache& cache,
                                                   MessageSink& status_sink,
                                                   const Path& exe_path) const override
        {
            (void)cache;
            (void)status_sink;
            Command cmd;
#if !defined(_WIN32)
            cmd.string_arg(cache.get_tool_path(Tools::MONO, status_sink));
#endif // ^^^ !_WIN32
            cmd.string_arg(exe_path).string_arg("help").string_arg("-ForceEnglishOutput");
            return run_to_extract_version(Tools::NUGET, exe_path, std::move(cmd))
#if !defined(_WIN32)
                .map_error([](LocalizedString&& error) {
                    return std::move(error.append_raw('\n').append(msgMonoInstructions));
                })
#endif // ^^^ !_WIN32

                .then([&](std::string&& output) {
                    // Sample output:
                    // NuGet Version: 4.6.2.5055
                    // usage: NuGet <command> [args] [options]
                    // Type 'NuGet help <command>' for help on a specific command.
                    return extract_prefixed_nonwhitespace("NuGet Version: ", Tools::NUGET, std::move(output), exe_path);
                });
        }
    };

    struct NodeProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::NODE; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::NODE}; }
        virtual std::array<int, 3> default_min_version() const override { return {16, 12, 0}; }

#if defined(_WIN32)
        virtual void add_system_paths(const ReadOnlyFilesystem&, std::vector<Path>& out_candidate_paths) const override
        {
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get()) out_candidate_paths.push_back(*pf / "nodejs" / "node.exe");
            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get()) out_candidate_paths.push_back(*pf / "nodejs" / "node.exe");
        }
#endif

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(Tools::NODE, exe_path, Command(exe_path).string_arg("--version"))
                .then([&](std::string&& output) {
                    // Sample output: v16.12.0
                    return extract_prefixed_nonwhitespace("v", Tools::NODE, std::move(output), exe_path);
                });
        }
    };

    struct GitProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::GIT; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::GIT}; }
        virtual std::array<int, 3> default_min_version() const override { return {2, 7, 4}; }

#if defined(_WIN32)
        virtual void add_system_paths(const ReadOnlyFilesystem&, std::vector<Path>& out_candidate_paths) const override
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

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(Tools::GIT, exe_path, Command(exe_path).string_arg("--version"))
                .then([&](std::string&& output) {
                    // Sample output: git version 2.17.1.windows.2
                    return extract_prefixed_nonwhitespace("git version ", Tools::GIT, std::move(output), exe_path);
                });
        }
    };

    struct MonoProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::MONO; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::MONO}; }
        virtual std::array<int, 3> default_min_version() const override { return {0, 0, 0}; }

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(Tools::MONO, exe_path, Command(exe_path).string_arg("--version"))
                .then([&](std::string&& output) {
                    // Sample output:
                    // Mono JIT compiler version 6.8.0.105 (Debian 6.8.0.105+dfsg-2 Wed Feb 26 23:23:50 UTC 2020)
                    return extract_prefixed_nonwhitespace(
                        "Mono JIT compiler version ", Tools::MONO, std::move(output), exe_path);
                });
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

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(Tools::GSUTIL, exe_path, Command(exe_path).string_arg("version"))
                .then([&](std::string&& output) {
                    // Sample output: gsutil version: 4.58
                    return extract_prefixed_nonwhitespace(
                        "gsutil version: ", Tools::GSUTIL, std::move(output), exe_path);
                });
        }
    };

    struct AwsCliProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::AWSCLI; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::AWSCLI}; }
        virtual std::array<int, 3> default_min_version() const override { return {2, 4, 4}; }

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(Tools::AWSCLI, exe_path, Command(exe_path).string_arg("--version"))
                .then([&](std::string&& output) {
                    // Sample output: aws-cli/2.4.4 Python/3.8.8 Windows/10 exe/AMD64 prompt/off
                    return extract_prefixed_nonwhitespace("aws-cli/", Tools::AWSCLI, std::move(output), exe_path);
                });
        }
    };

    struct AzCliProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::AZCLI; }
        virtual std::vector<StringView> system_exe_stems() const override { return {Tools::AZCLI}; }
        virtual std::array<int, 3> default_min_version() const override { return {2, 64, 0}; }

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(
                       Tools::AZCLI,
                       exe_path,
                       Command(exe_path).string_arg("version").string_arg("--output").string_arg("json"))
                .then([&](std::string&& output) {
                    // {
                    //    ...
                    //   "azure-cli": "2.64.0",
                    //    ...
                    // }

                    return extract_prefixed_nonquote("\"azure-cli\": \"", Tools::AZCLI, std::move(output), exe_path);
                });
        }
    };

    struct CosCliProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::COSCLI; }
        virtual std::vector<StringView> system_exe_stems() const override { return {"cos"}; }
        virtual std::array<int, 3> default_min_version() const override { return {0, 11, 0}; }

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(Tools::COSCLI, exe_path, Command(exe_path).string_arg("--version"))
                .then([&](std::string&& output) {
                    // Sample output: coscli version v0.11.0-beta
                    return extract_prefixed_nonwhitespace(
                        "coscli version v", Tools::COSCLI, std::move(output), exe_path);
                });
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

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(Tools::POWERSHELL_CORE, exe_path, Command(exe_path).string_arg("--version"))
                .then([&](std::string&& output) {
                    // Sample output: PowerShell 7.0.3\r\n
                    return extract_prefixed_nonwhitespace(
                        "PowerShell ", Tools::POWERSHELL_CORE, std::move(output), exe_path);
                });
        }
    };

    struct Python3Provider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::PYTHON3; }
        virtual std::vector<StringView> system_exe_stems() const override { return {"python3", "py3", "python", "py"}; }
        virtual std::array<int, 3> default_min_version() const override { return {3, 5, 0}; } // 3.5 added -m venv

#if defined(_WIN32)
        void add_system_paths_impl(const ReadOnlyFilesystem& fs,
                                   std::vector<Path>& out_candidate_paths,
                                   const Path& program_files_root) const
        {
            for (auto&& candidate : fs.get_directories_non_recursive(program_files_root, VCPKG_LINE_INFO))
            {
                auto name = candidate.filename();
                if (Strings::case_insensitive_ascii_starts_with(name, "Python") &&
                    std::all_of(name.begin() + 6, name.end(), ParserBase::is_ascii_digit))
                {
                    out_candidate_paths.emplace_back(std::move(candidate));
                }
            }
        }

        virtual void add_system_paths(const ReadOnlyFilesystem& fs,
                                      std::vector<Path>& out_candidate_paths) const override
        {
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get())
            {
                add_system_paths_impl(fs, out_candidate_paths, *pf);
            }

            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get())
            {
                add_system_paths_impl(fs, out_candidate_paths, *pf);
            }
        }
#endif // ^^^ _WIN32

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(Tools::PYTHON3, exe_path, Command(exe_path).string_arg("--version"))
                .then([&](std::string&& output) {
                    // Sample output: Python 3.10.2\r\n
                    return extract_prefixed_nonwhitespace("Python ", Tools::PYTHON3, std::move(output), exe_path);
                });
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
        virtual void add_system_paths(const ReadOnlyFilesystem&, std::vector<Path>& out_candidate_paths) const override
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

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(Tools::SEVEN_ZIP, exe_path, Command(exe_path))
                .then([&](std::string&& output) {
                    // Sample output: 7-Zip 24.09 (x64) : Copyright (c) 1999-2024 Igor Pavlov : 2024-11-29
                    return extract_prefixed_nonwhitespace("7-Zip ", Tools::SEVEN_ZIP, std::move(output), exe_path);
                });
        }
    };

    struct ToolCacheImpl final : ToolCache
    {
        const Filesystem& fs;
        AssetCachingSettings asset_cache_settings;
        const Path downloads;
        const Path config_path;
        const Path tools;
        const RequireExactVersions abiToolVersionHandling;

        vcpkg::Cache<std::string, PathAndVersion> path_version_cache;
        vcpkg::Lazy<std::vector<ToolDataEntry>> m_tool_data_cache;

        ToolCacheImpl(const Filesystem& fs,
                      const AssetCachingSettings& asset_cache_settings,
                      Path downloads,
                      Path config_path,
                      Path tools,
                      RequireExactVersions abiToolVersionHandling)
            : fs(fs)
            , asset_cache_settings(asset_cache_settings)
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
        Optional<PathAndVersion> find_first_with_sufficient_version(MessageSink& status_sink,
                                                                    const ToolProvider& tool_provider,
                                                                    const std::vector<Path>& candidates,
                                                                    Func&& accept_version,
                                                                    const Func2& log_candidate) const
        {
            for (auto&& candidate : candidates)
            {
                if (!fs.exists(candidate, IgnoreErrors{})) continue;
                if (!tool_provider.cheap_is_acceptable(candidate)) continue;
                auto maybe_version = tool_provider.get_version(*this, status_sink, candidate);
                log_candidate(candidate, maybe_version);
                const auto version = maybe_version.get();
                if (!version) continue;
                const auto parsed_version = parse_tool_version_string(*version);
                if (!parsed_version) continue;
                auto& actual_version = *parsed_version.get();
                if (!accept_version(actual_version)) continue;
                if (!tool_provider.is_acceptable(candidate)) continue;

                return PathAndVersion{candidate, *version};
            }

            return nullopt;
        }

        Path download_tool(const ToolData& tool_data, MessageSink& status_sink) const
        {
            using namespace Hash;

            const std::array<int, 3>& version = tool_data.version;
            const std::string version_as_string = fmt::format("{}.{}.{}", version[0], version[1], version[2]);
            Checks::msg_check_maybe_upgrade(VCPKG_LINE_INFO,
                                            !tool_data.url.empty(),
                                            msgToolOfVersionXNotFound,
                                            msg::tool_name = tool_data.name,
                                            msg::version = version_as_string);
            status_sink.println(Color::none,
                                msgDownloadingPortableToolVersionX,
                                msg::tool_name = tool_data.name,
                                msg::version = version_as_string);

            const auto download_path = downloads / tool_data.download_subpath;
            const auto hash_result = get_file_hash(console_diagnostic_context, fs, download_path, Algorithm::Sha512);
            switch (hash_result.prognosis)
            {
                case HashPrognosis::Success:
                    if (!Strings::case_insensitive_ascii_equals(tool_data.sha512, hash_result.hash))
                    {
                        Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                                      LocalizedString::from_raw(download_path)
                                                          .append_raw(": ")
                                                          .append_raw(ErrorPrefix)
                                                          .append(msgToolHashMismatch,
                                                                  msg::tool_name = tool_data.name,
                                                                  msg::expected = tool_data.sha512,
                                                                  msg::actual = hash_result.hash));
                    }

                    break;
                case HashPrognosis::FileNotFound:
                    if (!download_file_asset_cached(console_diagnostic_context,
                                                    null_sink,
                                                    asset_cache_settings,
                                                    fs,
                                                    tool_data.url,
                                                    {},
                                                    download_path,
                                                    tool_data.download_subpath,
                                                    tool_data.sha512))
                    {
                        Checks::exit_fail(VCPKG_LINE_INFO);
                    }
                    break;
                case HashPrognosis::OtherError: Checks::exit_fail(VCPKG_LINE_INFO);
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }

            const auto tool_dir_path = tools / tool_data.tool_dir_subpath;
            Path exe_path = tool_dir_path / tool_data.exe_subpath;

            if (tool_data.is_archive)
            {
                status_sink.println(Color::none, msgExtractingTool, msg::tool_name = tool_data.name);
                Path to_path_partial =
                    extract_archive_to_temp_subdirectory(fs, *this, status_sink, download_path, tool_dir_path);
                fs.rename_or_delete(to_path_partial, tool_dir_path, IgnoreErrors{});
            }
            else
            {
                fs.create_directories(exe_path.parent_path(), IgnoreErrors{});
                fs.rename_or_delete(download_path, exe_path, IgnoreErrors{});
            }

            if (!fs.exists(exe_path, IgnoreErrors{}))
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgExpectedPathToExist, msg::path = exe_path);
            }

            return exe_path;
        }

        virtual const Path& get_tool_path(StringView tool, MessageSink& status_sink) const override
        {
            return get_tool_pathversion(tool, status_sink).p;
        }

        PathAndVersion get_path(const ToolProvider& tool, MessageSink& status_sink) const
        {
            const bool env_force_system_binaries =
                get_environment_variable(EnvironmentVariableVcpkgForceSystemBinaries).has_value();
            const bool env_force_download_binaries =
                get_environment_variable(EnvironmentVariableVcpkgForceDownloadedBinaries).has_value();
            const auto maybe_tool_data = get_tool_data(load_tool_data(), tool.tool_data_name());
            const bool download_available = maybe_tool_data.has_value() && !maybe_tool_data.get()->url.empty();
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
                tool.add_system_paths(fs, candidate_paths);
            }

            std::string considered_versions;
            if (ignore_version)
            {
                // If we are forcing the system copy (and therefore ignoring versions), take the first entry that
                // is acceptable.
                const auto it =
                    std::find_if(candidate_paths.begin(), candidate_paths.end(), [this, &tool](const Path& p) {
                        return this->fs.is_regular_file(p) && tool.is_acceptable(p);
                    });

                if (it != candidate_paths.end())
                {
                    return {*it, "0"};
                }
            }
            else
            {
                // Otherwise, execute each entry and compare its version against the constraint. Take the first that
                // matches.
                const auto maybe_path = find_first_with_sufficient_version(
                    status_sink,
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
                    [&](const auto& path, const ExpectedL<std::string>& maybe_version) {
                        considered_versions += fmt::format("{}: {}\n",
                                                           path,
                                                           maybe_version.has_value() ? *maybe_version.get()
                                                                                     : maybe_version.error().data());
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
                    auto downloaded_path = download_tool(*tool_data, status_sink);
                    auto downloaded_version =
                        tool.get_version(*this, status_sink, downloaded_path).value_or_exit(VCPKG_LINE_INFO);
                    return {std::move(downloaded_path), std::move(downloaded_version)};
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
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, s);
        }

        const PathAndVersion& get_tool_pathversion(StringView tool, MessageSink& status_sink) const
        {
            return path_version_cache.get_lazy(tool, [&]() -> PathAndVersion {
                // First deal with specially handled tools.
                // For these we may look in locations like Program Files, the PATH etc as well as the auto-downloaded
                // location.
                if (tool == Tools::CMAKE) return get_path(CMakeProvider(), status_sink);
                if (tool == Tools::GIT) return get_path(GitProvider(), status_sink);
                if (tool == Tools::NINJA) return get_path(NinjaProvider(), status_sink);
                if (tool == Tools::POWERSHELL_CORE) return get_path(PowerShellCoreProvider(), status_sink);
                if (tool == Tools::NUGET) return get_path(NuGetProvider(), status_sink);
                if (tool == Tools::NODE) return get_path(NodeProvider(), status_sink);
                if (tool == Tools::MONO) return get_path(MonoProvider(), status_sink);
                if (tool == Tools::GSUTIL) return get_path(GsutilProvider(), status_sink);
                if (tool == Tools::AWSCLI) return get_path(AwsCliProvider(), status_sink);
                if (tool == Tools::AZCOPY) return get_path(AzCopyProvider(), status_sink);
                if (tool == Tools::AZCLI) return get_path(AzCliProvider(), status_sink);
                if (tool == Tools::COSCLI) return get_path(CosCliProvider(), status_sink);
                if (tool == Tools::PYTHON3) return get_path(Python3Provider(), status_sink);
                if (tool == Tools::PYTHON3_WITH_VENV) return get_path(Python3WithVEnvProvider(), status_sink);
                if (tool == Tools::SEVEN_ZIP || tool == Tools::SEVEN_ZIP_ALT)
                {
                    return get_path(SevenZipProvider(), status_sink);
                }
                if (tool == Tools::TAR)
                {
                    return {find_system_tar(fs).value_or_exit(VCPKG_LINE_INFO), {}};
                }
                if (tool == Tools::CMAKE_SYSTEM)
                {
                    return {find_system_cmake(fs).value_or_exit(VCPKG_LINE_INFO), {}};
                }
                GenericToolProvider provider{tool};
                return get_path(provider, status_sink);
            });
        }

        virtual const std::string& get_tool_version(StringView tool, MessageSink& status_sink) const override
        {
            return get_tool_pathversion(tool, status_sink).version;
        }

        std::vector<ToolDataEntry> load_tool_data() const
        {
            return m_tool_data_cache.get_lazy([&]() {
                auto maybe_tool_data = parse_tool_data_file(fs, config_path);
                if (auto tool_data = maybe_tool_data.get())
                {
                    return std::move(*tool_data);
                }
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, maybe_tool_data.error());
            });
        }
    };

    ExpectedL<Path> find_system_tar(const ReadOnlyFilesystem& fs)
    {
#if defined(_WIN32)
        const auto& maybe_system32 = get_system32();
        if (auto system32 = maybe_system32.get())
        {
            auto shipped_with_windows = *system32 / "tar.exe";
            if (fs.is_regular_file(shipped_with_windows))
            {
                return shipped_with_windows;
            }
        }
        else
        {
            return maybe_system32.error();
        }
#endif // ^^^ _WIN32

        const auto tools = fs.find_from_PATH(Tools::TAR);
        if (tools.empty())
        {
            return msg::format_error(msgToolFetchFailed, msg::tool_name = Tools::TAR)
#if defined(_WIN32)
                .append(msgToolInWin10)
#else
                .append(msgInstallWithSystemManager)
#endif
                ;
        }
        else
        {
            return tools[0];
        }
    }

    ExpectedL<Path> find_system_cmake(const ReadOnlyFilesystem& fs)
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

        return msg::format_error(msgToolFetchFailed, msg::tool_name = Tools::CMAKE)
#if !defined(_WIN32)
            .append(msgInstallWithSystemManager)
#endif
            ;
    }

    std::unique_ptr<ToolCache> get_tool_cache(const Filesystem& fs,
                                              const AssetCachingSettings& asset_cache_settings,
                                              Path downloads,
                                              Path config_path,
                                              Path tools,
                                              RequireExactVersions abiToolVersionHandling)
    {
        return std::make_unique<ToolCacheImpl>(
            fs, asset_cache_settings, downloads, config_path, tools, abiToolVersionHandling);
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
