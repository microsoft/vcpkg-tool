#include <vcpkg/base/cache.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/lazy.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/archives.h>
#include <vcpkg/tools.h>
#include <vcpkg/tools.test.h>
#include <vcpkg/versions.h>

#include <regex>
#include "vcpkg/base/fwd/system.h"

namespace vcpkg
{
    // /\d+\.\d+(\.\d+)?/
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

    static Optional<ToolData> parse_tool_data_from_xml(StringView XML, StringView XML_PATH, StringView tool)
    {
#if defined(_WIN32)
        return parse_tool_data_from_xml(XML, XML_PATH, tool, "windows", to_zstring_view(get_host_processor()));
#elif defined(__APPLE__)
        return parse_tool_data_from_xml(XML, XML_PATH, tool, "osx", to_zstring_view(get_host_processor()));
#elif defined(__linux__)
        return parse_tool_data_from_xml(XML, XML_PATH, tool, "linux", to_zstring_view(get_host_processor()));
#elif defined(__FreeBSD__)
        return parse_tool_data_from_xml(XML, XML_PATH, tool, "freebsd", to_zstring_view(get_host_processor()));
#elif defined(__OpenBSD__)
        return parse_tool_data_from_xml(XML, XML_PATH, tool, "openbsd", to_zstring_view(get_host_processor()));
#else
        return nullopt;
#endif
    }

    Optional<ToolData> parse_tool_data_from_xml(StringView XML, StringView XML_PATH, StringView tool, StringView os, StringView host_processor)
    {
        static const char* XML_VERSION = "2";
        static const std::regex XML_VERSION_REGEX{R"###(<tools[\s]+version="([^"]+)">)###"};
        std::cmatch match_xml_version;
        const bool has_xml_version = std::regex_search(XML.begin(), XML.end(), match_xml_version, XML_VERSION_REGEX);
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               has_xml_version,
                               msgCouldNotFindToolVersion,
                               msg::version = XML_VERSION,
                               msg::path = XML_PATH);
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               XML_VERSION == match_xml_version[1],
                               msgVersionConflictXML,
                               msg::path = XML_PATH,
                               msg::expected_version = XML_VERSION,
                               msg::actual_version = match_xml_version[1].str());

        const std::regex tool_regex{fmt::format(R"###(<tool[\s]+name="{}"[\s]+os="{}" arch="{}>)###", tool, os, host_processor)};
        std::cmatch match_tool_entry;
        const bool has_tool_entry = std::regex_search(XML.begin(), XML.end(), match_tool_entry, tool_regex);
        if (!has_tool_entry) return nullopt;

        const std::string tool_data =
            Strings::find_exactly_one_enclosed(XML, match_tool_entry[0].str(), "</tool>").to_string();
        const std::string version_as_string =
            Strings::find_exactly_one_enclosed(tool_data, "<version>", "</version>").to_string();
        const std::string exe_relative_path =
            Strings::find_exactly_one_enclosed(tool_data, "<exeRelativePath>", "</exeRelativePath>").to_string();
        const std::string url = Strings::find_exactly_one_enclosed(tool_data, "<url>", "</url>").to_string();
        const std::string sha512 = Strings::find_exactly_one_enclosed(tool_data, "<sha512>", "</sha512>").to_string();
        auto archive_name = Strings::find_at_most_one_enclosed(tool_data, "<archiveName>", "</archiveName>");

        const Optional<std::array<int, 3>> version = parse_tool_version_string(version_as_string);
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               version.has_value(),
                               msgFailedToParseVersionXML,
                               msg::tool_name = tool,
                               msg::version = version_as_string);

        Path tool_dir_name = fmt::format("{}-{}-{}", tool, version_as_string, os);
        Path download_subpath;
        if (auto a = archive_name.get())
        {
            download_subpath = a->to_string();
        }
        else if (!exe_relative_path.empty())
        {
            download_subpath = Strings::concat(sha512.substr(0, 8), '-', exe_relative_path);
        }

        return ToolData{tool.to_string(),
                        *version.get(),
                        exe_relative_path,
                        url,
                        download_subpath,
                        archive_name.has_value(),
                        tool_dir_name,
                        sha512};
    }

    struct PathAndVersion
    {
        Path p;
        std::string version;
    };

    static ExpectedL<std::string> run_to_extract_version(StringLiteral tool_name, const Path& exe_path, Command&& cmd)
    {
        return flatten_out(cmd_execute_and_capture_output(cmd), exe_path).map_error([&](LocalizedString&& output) {
            return msg::format_error(
                       msgFailedToRunToolToDetermineVersion, msg::tool_name = tool_name, msg::path = exe_path)
                .append_raw('\n')
                .append(output);
        });
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
            if (end_idx != std::string::npos)
            {
                output.resize(end_idx);
            }

            output.erase(0, idx);
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
            cmd.string_arg(exe_path);
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

    struct Aria2Provider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::ARIA2; }
        virtual std::vector<StringView> system_exe_stems() const override { return {"aria2c"}; }
        virtual std::array<int, 3> default_min_version() const override { return {1, 33, 1}; }
        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            return run_to_extract_version(Tools::ARIA2, exe_path, Command(exe_path).string_arg("--version"))
                .then([&](std::string&& output) {
                    // Sample output:
                    // aria2 version 1.35.0
                    // Copyright (C) 2006, 2019 Tatsuhiro Tsujikawa
                    return extract_prefixed_nonwhitespace("aria2 version ", Tools::ARIA2, std::move(output), exe_path);
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

    struct IfwInstallerBaseProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return "installerbase"; }
        virtual std::array<int, 3> default_min_version() const override { return {0, 0, 0}; }

        virtual ExpectedL<std::string> get_version(const ToolCache&, MessageSink&, const Path& exe_path) const override
        {
            // Sample output: 3.1.81
            return run_to_extract_version(
                Tools::IFW_INSTALLER_BASE, exe_path, Command(exe_path).string_arg("--framework-version"));
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
                               Command(exe_path).string_arg("-m").string_arg("venv").string_arg("-h")),
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

    struct ToolCacheImpl final : ToolCache
    {
        const Filesystem& fs;
        const std::shared_ptr<const DownloadManager> downloader;
        const Path downloads;
        const Path xml_config;
        const Path tools;
        const RequireExactVersions abiToolVersionHandling;

        vcpkg::Lazy<std::string> xml_config_contents;
        vcpkg::Cache<std::string, PathAndVersion> path_version_cache;

        ToolCacheImpl(const Filesystem& fs,
                      const std::shared_ptr<const DownloadManager>& downloader,
                      Path downloads,
                      Path xml_config,
                      Path tools,
                      RequireExactVersions abiToolVersionHandling)
            : fs(fs)
            , downloader(downloader)
            , downloads(std::move(downloads))
            , xml_config(std::move(xml_config))
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
            if (!fs.exists(download_path, IgnoreErrors{}))
            {
                status_sink.println(Color::none,
                                    msgDownloadingTool,
                                    msg::tool_name = tool_data.name,
                                    msg::url = tool_data.url,
                                    msg::path = download_path);

                downloader->download_file(fs, tool_data.url, {}, download_path, tool_data.sha512, null_sink);
            }
            else
            {
                verify_downloaded_file_hash(fs, tool_data.url, download_path, tool_data.sha512);
            }

            const auto tool_dir_path = tools / tool_data.tool_dir_subpath;
            Path exe_path = tool_dir_path / tool_data.exe_subpath;

            if (tool_data.is_archive)
            {
                status_sink.println(Color::none, msgExtractingTool, msg::tool_name = tool_data.name);
#if defined(_WIN32)
                if (tool_data.name == "cmake")
                {
                    // We use cmake as the core extractor on Windows, so we need to perform a special dance when
                    // extracting it.
                    win32_extract_bootstrap_zip(fs, *this, status_sink, download_path, tool_dir_path);
                }
                else
#endif // ^^^ _WIN32
                {
                    set_directory_to_archive_contents(fs, *this, status_sink, download_path, tool_dir_path);
                }
            }
            else
            {
                fs.create_directories(exe_path.parent_path(), IgnoreErrors{});
                fs.rename(download_path, exe_path, IgnoreErrors{});
            }

            if (!fs.exists(exe_path, IgnoreErrors{}))
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgExpectedPathToExist, msg::path = exe_path);
            }

            return exe_path;
        }

        const std::string& get_config_contents() const
        {
            return xml_config_contents.get_lazy(
                [this]() { return this->fs.read_contents(this->xml_config, VCPKG_LINE_INFO); });
        }

        virtual const Path& get_tool_path(StringView tool, MessageSink& status_sink) const override
        {
            return get_tool_pathversion(tool, status_sink).p;
        }

        static constexpr StringLiteral s_env_vcpkg_force_system_binaries = "VCPKG_FORCE_SYSTEM_BINARIES";

        PathAndVersion get_path(const ToolProvider& tool, MessageSink& status_sink) const
        {
            const bool env_force_system_binaries =
                get_environment_variable(s_env_vcpkg_force_system_binaries).has_value();
            const bool env_force_download_binaries =
                get_environment_variable("VCPKG_FORCE_DOWNLOADED_BINARIES").has_value();
            const auto maybe_tool_data =
                parse_tool_data_from_xml(get_config_contents(), xml_config, tool.tool_data_name());

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
                // If there is an entry for the tool in vcpkgTools.xml, use that version as the minimum
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
                                         msg::env_var = format_environment_variable(s_env_vcpkg_force_system_binaries));
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
                if (tool == Tools::ARIA2) return get_path(Aria2Provider(), status_sink);
                if (tool == Tools::NODE) return get_path(NodeProvider(), status_sink);
                if (tool == Tools::IFW_INSTALLER_BASE) return get_path(IfwInstallerBaseProvider(), status_sink);
                if (tool == Tools::MONO) return get_path(MonoProvider(), status_sink);
                if (tool == Tools::GSUTIL) return get_path(GsutilProvider(), status_sink);
                if (tool == Tools::AWSCLI) return get_path(AwsCliProvider(), status_sink);
                if (tool == Tools::COSCLI) return get_path(CosCliProvider(), status_sink);
                if (tool == Tools::PYTHON3) return get_path(Python3Provider(), status_sink);
                if (tool == Tools::PYTHON3_WITH_VENV) return get_path(Python3WithVEnvProvider(), status_sink);
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
                                              std::shared_ptr<const DownloadManager> downloader,
                                              Path downloads,
                                              Path xml_config,
                                              Path tools,
                                              RequireExactVersions abiToolVersionHandling)
    {
        return std::make_unique<ToolCacheImpl>(
            fs, std::move(downloader), downloads, xml_config, tools, abiToolVersionHandling);
    }
}
