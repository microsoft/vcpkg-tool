#include <vcpkg/base/cache.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/lazy.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h>
#include <vcpkg/tools.h>
#include <vcpkg/tools.test.h>
#include <vcpkg/versions.h>

#include <regex>

namespace vcpkg
{
    DECLARE_AND_REGISTER_MESSAGE(ToolFetchFailed, (msg::tool_name), "", "Could not fetch {tool_name}.");
    DECLARE_AND_REGISTER_MESSAGE(ToolInWin10, (), "", "This utility is bundled with Windows 10 or later.");
    DECLARE_AND_REGISTER_MESSAGE(
        DownloadAvailable,
        (msg::env_var),
        "",
        "A downloadable copy of this tool is available and can be used by unsetting {env_var}.");
    DECLARE_AND_REGISTER_MESSAGE(UnknownTool,
                                 (),
                                 "",
                                 "vcpkg does not have a definition of this tool for this platform.");

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
        return parse_tool_data_from_xml(XML, XML_PATH, tool, "windows");
#elif defined(__APPLE__)
        return parse_tool_data_from_xml(XML, XML_PATH, tool, "osx");
#elif defined(__linux__)
        return parse_tool_data_from_xml(XML, XML_PATH, tool, "linux");
#elif defined(__FreeBSD__)
        return parse_tool_data_from_xml(XML, XML_PATH, tool, "freebsd");
#elif defined(__OpenBSD__)
        return parse_tool_data_from_xml(XML, XML_PATH, tool, "openbsd");
#else
        return nullopt;
#endif
    }

    Optional<ToolData> parse_tool_data_from_xml(StringView XML, StringView XML_PATH, StringView tool, StringView os)
    {
        static const std::string XML_VERSION = "2";
        static const std::regex XML_VERSION_REGEX{R"###(<tools[\s]+version="([^"]+)">)###"};
        std::cmatch match_xml_version;
        const bool has_xml_version = std::regex_search(XML.begin(), XML.end(), match_xml_version, XML_VERSION_REGEX);
        Checks::check_exit(
            VCPKG_LINE_INFO, has_xml_version, R"(Could not find <tools version="%s"> in %s)", XML_VERSION, XML_PATH);
        Checks::check_exit(VCPKG_LINE_INFO,
                           XML_VERSION == match_xml_version[1],
                           "Expected %s version: [%s], but was [%s]. Please re-run bootstrap-vcpkg.",
                           XML_PATH,
                           XML_VERSION,
                           match_xml_version[1]);

        const std::regex tool_regex{Strings::format(R"###(<tool[\s]+name="%s"[\s]+os="%s">)###", tool, os)};
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
        Checks::check_exit(VCPKG_LINE_INFO,
                           version.has_value(),
                           "Could not parse version for tool %s. Version string was: %s",
                           tool,
                           version_as_string);

        Path tool_dir_name = Strings::format("%s-%s-%s", tool, version_as_string, os);
        Path download_subpath;
        if (auto a = archive_name.get())
        {
            download_subpath = a->to_string();
        }
        else if (!exe_relative_path.empty())
        {
            download_subpath = Strings::concat(sha512.substr(0, 8), '-', exe_relative_path);
        }

        return ToolData{
            *version.get(), exe_relative_path, url, download_subpath, archive_name.has_value(), tool_dir_name, sha512};
    }

    struct PathAndVersion
    {
        Path p;
        std::string version;
    };

    DECLARE_AND_REGISTER_MESSAGE(InstallWithSystemManager,
                                 (),
                                 "",
                                 "You may be able to install this tool via your system package manager.");

    DECLARE_AND_REGISTER_MESSAGE(
        InstallWithSystemManagerPkg,
        (msg::command_line),
        "",
        "You may be able to install this tool via your system package manager ({command_line}).");

    struct ToolProvider
    {
        virtual StringView tool_data_name() const = 0;
        /// \returns The stem of the executable to search PATH for, or empty string if tool can't be searched
        virtual StringView system_exe_stem() const = 0;
        virtual std::array<int, 3> default_min_version() const = 0;
        virtual bool is_abi_sensitive() const = 0;

        virtual void add_system_paths(std::vector<Path>& out_candidate_paths) const { (void)out_candidate_paths; }
        virtual ExpectedS<std::string> get_version(const ToolCache& cache, const Path& exe_path) const = 0;

        virtual void add_system_package_info(LocalizedString& out) const
        {
            out.append_raw(" ").append(msgInstallWithSystemManager);
        }

        bool is_system_searchable() const { return !system_exe_stem().empty(); }
    };

    struct GenericToolProvider : ToolProvider
    {
        explicit GenericToolProvider(StringView tool) : m_tool_data_name(tool) { }

        const StringView m_tool_data_name;

        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return m_tool_data_name; }
        virtual StringView system_exe_stem() const override { return ""; }
        virtual std::array<int, 3> default_min_version() const override { return {0}; }

        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path&) const override
        {
            return {"0", expected_left_tag};
        }
    };

    struct CMakeProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return true; }
        virtual StringView tool_data_name() const override { return Tools::CMAKE; }
        virtual StringView system_exe_stem() const override { return Tools::CMAKE; }
        virtual std::array<int, 3> default_min_version() const override { return {3, 17, 1}; }

#if defined(_WIN32)
        virtual void add_system_paths(std::vector<Path>& out_candidate_paths) const override
        {
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get()) out_candidate_paths.push_back(*pf / "CMake" / "bin" / "cmake.exe");
            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get())
                out_candidate_paths.push_back(*pf / "CMake" / "bin" / "cmake.exe");
        }
#endif
        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", exe_path, "\n"),
                        expected_right_tag};
            }

            /* Sample output:
cmake version 3.10.2

CMake suite maintained and supported by Kitware (kitware.com/cmake).
                */

            // There are two expected output formats to handle: "cmake3 version x.x.x" and "cmake version x.x.x"
            auto simplifiedOutput = Strings::replace_all(rc.output, "cmake3", "cmake");
            return {Strings::find_exactly_one_enclosed(simplifiedOutput, "cmake version ", "\n").to_string(),
                    expected_left_tag};
        }
    };

    struct NinjaProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::NINJA; }
        virtual StringView system_exe_stem() const override { return Tools::NINJA; }
        virtual std::array<int, 3> default_min_version() const override { return {3, 5, 1}; }

        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", exe_path, "\n"),
                        expected_right_tag};
            }

            /* Sample output:
1.8.2
                */
            return {std::move(rc.output), expected_left_tag};
        }
    };

    struct NuGetProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::NUGET; }
        virtual StringView system_exe_stem() const override { return Tools::NUGET; }
        virtual std::array<int, 3> default_min_version() const override { return {4, 6, 2}; }

        virtual ExpectedS<std::string> get_version(const ToolCache& cache, const Path& exe_path) const override
        {
            Command cmd;
#ifndef _WIN32
            cmd.string_arg(cache.get_tool_path(Tools::MONO));
#else
            (void)cache;
#endif
            cmd.string_arg(exe_path);
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
#ifndef _WIN32
                return {Strings::concat(
                            std::move(rc.output),
                            "\n\nFailed to get version of ",
                            exe_path,
                            "\nThis may be caused by an incomplete mono installation. Full mono is "
                            "available on some systems via `sudo apt install mono-complete`. Ubuntu 18.04 users may "
                            "need a newer version of mono, available at https://www.mono-project.com/download/stable/"),
                        expected_right_tag};
#else
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", exe_path, "\n"),
                        expected_right_tag};
#endif
            }

            /* Sample output:
NuGet Version: 4.6.2.5055
usage: NuGet <command> [args] [options]
Type 'NuGet help <command>' for help on a specific command.

[[[List of available commands follows]]]
                */
            return {Strings::find_exactly_one_enclosed(rc.output, "NuGet Version: ", "\n").to_string(),
                    expected_left_tag};
        }
    };

    struct Aria2Provider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return "aria2"; }
        virtual StringView system_exe_stem() const override { return "aria2c"; }
        virtual std::array<int, 3> default_min_version() const override { return {1, 33, 1}; }
        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", exe_path, "\n"),
                        expected_right_tag};
            }

            /* Sample output:
aria2 version 1.35.0
Copyright (C) 2006, 2019 Tatsuhiro Tsujikawa
[...]
                */
            const auto idx = rc.output.find("aria2 version ");
            Checks::check_exit(
                VCPKG_LINE_INFO, idx != std::string::npos, "Unexpected format of aria2 version string: %s", rc.output);
            auto start = rc.output.begin() + idx;
            char newlines[] = "\r\n";
            auto end = std::find_first_of(start, rc.output.end(), &newlines[0], &newlines[2]);
            return {std::string(start, end), expected_left_tag};
        }
    };

    struct NodeProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::NODE; }
        virtual StringView system_exe_stem() const override { return Tools::NODE; }
        virtual std::array<int, 3> default_min_version() const override { return {16, 12, 0}; }

#if defined(_WIN32)
        virtual void add_system_paths(std::vector<Path>& out_candidate_paths) const override
        {
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get()) out_candidate_paths.push_back(*pf / "nodejs" / "node.exe");
            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get()) out_candidate_paths.push_back(*pf / "nodejs" / "node.exe");
        }
#endif

        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", Tools::NODE, "\n"),
                        expected_right_tag};
            }

            // Sample output: v16.12.0
            auto start = rc.output.begin();
            if (start == rc.output.end() || *start != 'v')
            {
                return {Strings::concat(std::move(rc.output), "\n\nUnexpected output of ", Tools::NODE, " --version\n"),
                        expected_right_tag};
            }

            ++start;
            char newlines[] = "\r\n";
            auto end = std::find_first_of(start, rc.output.end(), &newlines[0], &newlines[2]);
            return {std::string(start, end), expected_left_tag};
        }
    };

    struct GitProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::GIT; }
        virtual StringView system_exe_stem() const override { return Tools::GIT; }
        virtual std::array<int, 3> default_min_version() const override { return {2, 7, 4}; }

#if defined(_WIN32)
        virtual void add_system_paths(std::vector<Path>& out_candidate_paths) const override
        {
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get()) out_candidate_paths.push_back(*pf / "git" / "cmd" / "git.exe");
            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get())
                out_candidate_paths.push_back(*pf / "git" / "cmd" / "git.exe");
        }
#endif

        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", exe_path, "\n"),
                        expected_right_tag};
            }

            /* Sample output:
git version 2.17.1.windows.2
                */
            const auto idx = rc.output.find("git version ");
            Checks::check_exit(
                VCPKG_LINE_INFO, idx != std::string::npos, "Unexpected format of git version string: %s", rc.output);
            return {rc.output.substr(idx), expected_left_tag};
        }
    };

    DECLARE_AND_REGISTER_MESSAGE(InstallWithSystemManagerMono,
                                 (msg::url),
                                 "",
                                 "Ubuntu 18.04 users may need a newer version of mono, available at {url}.");

    struct MonoProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::MONO; }
        virtual StringView system_exe_stem() const override { return Tools::MONO; }
        virtual std::array<int, 3> default_min_version() const override { return {0, 0, 0}; }

        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path& exe_path) const override
        {
            auto rc = cmd_execute_and_capture_output(Command(exe_path).string_arg("--version"));
            if (rc.exit_code != 0)
            {
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", exe_path, "\n"),
                        expected_right_tag};
            }

            /* Sample output:
Mono JIT compiler version 6.8.0.105 (Debian 6.8.0.105+dfsg-2 Wed Feb 26 23:23:50 UTC 2020)
                */
            const auto idx = rc.output.find("Mono JIT compiler version ");
            Checks::check_exit(
                VCPKG_LINE_INFO, idx != std::string::npos, "Unexpected format of mono version string: %s", rc.output);
            return {rc.output.substr(idx), expected_left_tag};
        }

        virtual void add_system_package_info(LocalizedString& out) const override
        {
#if defined(__APPLE__)
            out.append_raw(" ").append(msgInstallWithSystemManagerPkg, msg::command_line = "brew install mono");
#else
            out.append_raw(" ").append(msgInstallWithSystemManagerPkg,
                                       msg::command_line = "sudo apt install mono-complete");
            out.append_raw(" ").append(msgInstallWithSystemManagerMono,
                                       msg::url = "https://www.mono-project.com/download/stable/");
#endif
        }
    };

    struct GsutilProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::GSUTIL; }
        virtual StringView system_exe_stem() const override { return Tools::GSUTIL; }
        virtual std::array<int, 3> default_min_version() const override { return {4, 56, 0}; }

        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", exe_path, "\n"),
                        expected_right_tag};
            }

            /* Sample output:
gsutil version: 4.58
                */

            const auto idx = rc.output.find("gsutil version: ");
            Checks::check_exit(
                VCPKG_LINE_INFO, idx != std::string::npos, "Unexpected format of gsutil version string: %s", rc.output);
            return {rc.output.substr(idx), expected_left_tag};
        }
    };

    struct AwsCliProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::AWSCLI; }
        virtual StringView system_exe_stem() const override { return Tools::AWSCLI; }
        virtual std::array<int, 3> default_min_version() const override { return {2, 4, 4}; }

        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", exe_path, "\n"),
                        expected_right_tag};
            }

            /* Sample output:
aws-cli/2.4.4 Python/3.8.8 Windows/10 exe/AMD64 prompt/off
                */

            const auto idx = rc.output.find("aws-cli/");
            Checks::check_exit(
                VCPKG_LINE_INFO, idx != std::string::npos, "Unexpected format of awscli version string: %s", rc.output);
            return {rc.output.substr(idx), expected_left_tag};
        }
    };

    struct CosCliProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return Tools::COSCLI; }
        virtual StringView system_exe_stem() const override { return "cos"; }
        virtual std::array<int, 3> default_min_version() const override { return {0, 11, 0}; }

        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", exe_path, "\n"),
                        expected_right_tag};
            }

            /* Sample output:
coscli version v0.11.0-beta
                */

            const auto idx = rc.output.find("coscli version v");
            Checks::check_exit(
                VCPKG_LINE_INFO, idx != std::string::npos, "Unexpected format of coscli version string: %s", rc.output);
            return {rc.output.substr(idx), expected_left_tag};
        }
    };

    struct IfwInstallerBaseProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return false; }
        virtual StringView tool_data_name() const override { return "installerbase"; }
        virtual StringView system_exe_stem() const override { return ""; }
        virtual std::array<int, 3> default_min_version() const override { return {0, 0, 0}; }

        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--framework-version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", exe_path, "\n"),
                        expected_right_tag};
            }

            /* Sample output:
3.1.81
                */
            return {std::move(rc.output), expected_left_tag};
        }
    };

    struct PowerShellCoreProvider : ToolProvider
    {
        virtual bool is_abi_sensitive() const override { return true; }
        virtual StringView tool_data_name() const override { return "powershell-core"; }
        virtual StringView system_exe_stem() const override { return "pwsh"; }
        virtual std::array<int, 3> default_min_version() const override { return {7, 0, 3}; }

        virtual ExpectedS<std::string> get_version(const ToolCache&, const Path& exe_path) const override
        {
            auto rc = cmd_execute_and_capture_output(Command(exe_path).string_arg("--version"));
            if (rc.exit_code != 0)
            {
                return {Strings::concat(std::move(rc.output), "\n\nFailed to get version of ", exe_path, "\n"),
                        expected_right_tag};
            }

            // Sample output: PowerShell 7.0.3\r\n
            auto output = std::move(rc.output);
            if (!Strings::starts_with(output, "PowerShell "))
            {
                return {Strings::concat("Unexpected format of powershell-core version string: ", output),
                        expected_right_tag};
            }

            output.erase(0, 11);
            return {Strings::trim(std::move(output)), expected_left_tag};
        }
    };

    struct ToolCacheImpl final : ToolCache
    {
        Filesystem& fs;
        const DownloadManager& downloader;
        const Path downloads;
        const Path xml_config;
        const Path tools;
        const RequireExactVersions abiToolVersionHandling;

        vcpkg::Lazy<std::string> xml_config_contents;
        vcpkg::Cache<std::string, PathAndVersion> path_version_cache;

        ToolCacheImpl(Filesystem& fs,
                      const DownloadManager& downloader,
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

        template<typename Func>
        Optional<PathAndVersion> find_first_with_sufficient_version(const ToolProvider& tool_provider,
                                                                    const std::vector<Path>& candidates,
                                                                    Func&& accept_version) const
        {
            for (auto&& candidate : candidates)
            {
                if (!fs.exists(candidate, IgnoreErrors{})) continue;
                auto maybe_version = tool_provider.get_version(*this, candidate);
                const auto version = maybe_version.get();
                if (!version) continue;
                const auto parsed_version = parse_tool_version_string(*version);
                if (!parsed_version) continue;
                auto& actual_version = *parsed_version.get();
                if (!accept_version(actual_version)) continue;

                return PathAndVersion{candidate, *version};
            }

            return nullopt;
        }

        Path download_tool(StringView tool_name, const ToolData& tool_data) const
        {
            const std::array<int, 3>& version = tool_data.version;
            const std::string version_as_string = Strings::format("%d.%d.%d", version[0], version[1], version[2]);
            Checks::check_maybe_upgrade(
                VCPKG_LINE_INFO,
                !tool_data.url.empty(),
                "A suitable version of %s was not found (required v%s) and unable to automatically "
                "download a portable one. Please install a newer version of %s.",
                tool_name,
                version_as_string,
                tool_name);
            vcpkg::printf("A suitable version of %s was not found (required v%s). Downloading portable %s v%s...\n",
                          tool_name,
                          version_as_string,
                          tool_name,
                          version_as_string);
            const auto download_path = downloads / tool_data.download_subpath;
            if (!fs.exists(download_path, IgnoreErrors{}))
            {
                print2("Downloading ", tool_name, "...\n");
                print2("  ", tool_data.url, " -> ", download_path, "\n");
                downloader.download_file(fs, tool_data.url, download_path, tool_data.sha512);
            }
            else
            {
                verify_downloaded_file_hash(fs, tool_data.url, download_path, tool_data.sha512);
            }

            const auto tool_dir_path = tools / tool_data.tool_dir_subpath;
            Path exe_path = tool_dir_path / tool_data.exe_subpath;

            if (tool_data.is_archive)
            {
                print2("Extracting ", tool_name, "...\n");
#if defined(_WIN32)
                if (tool_name == "cmake")
                {
                    // We use cmake as the core extractor on Windows, so we need to perform a special dance when
                    // extracting it.
                    win32_extract_bootstrap_zip(fs, *this, download_path, tool_dir_path);
                }
                else
#endif // ^^^ _WIN32
                {
                    extract_archive(fs, *this, download_path, tool_dir_path);
                }
            }
            else
            {
                fs.create_directories(exe_path.parent_path(), IgnoreErrors{});
                fs.rename(download_path, exe_path, IgnoreErrors{});
            }

            Checks::check_exit(
                VCPKG_LINE_INFO, fs.exists(exe_path, IgnoreErrors{}), "Expected %s to exist after fetching", exe_path);

            return exe_path;
        }

        const std::string& get_config_contents() const
        {
            return xml_config_contents.get_lazy(
                [this]() { return this->fs.read_contents(this->xml_config, VCPKG_LINE_INFO); });
        }

        virtual const Path& get_tool_path(StringView tool) const override { return get_tool_pathversion(tool).p; }

        static constexpr StringLiteral s_env_vcpkg_force_system_binaries = "VCPKG_FORCE_SYSTEM_BINARIES";

        PathAndVersion get_path(const ToolProvider& tool) const
        {
            const bool env_force_system_binaries =
                get_environment_variable(s_env_vcpkg_force_system_binaries).has_value();
            const bool env_force_download_binaries =
                get_environment_variable("VCPKG_FORCE_DOWNLOADED_BINARIES").has_value();
            const auto maybe_tool_data =
                parse_tool_data_from_xml(get_config_contents(), xml_config, tool.tool_data_name());

            const bool download_available = maybe_tool_data.has_value() && !maybe_tool_data.get()->url.empty();
            // search for system searchable tools unless forcing downloads and download available
            const bool consider_system =
                tool.is_system_searchable() && !(env_force_download_binaries && download_available);
            // search for downloaded tools unless forcing system search
            const bool consider_downloads = !env_force_system_binaries || !consider_system;

            const bool exact_version = tool.is_abi_sensitive() && abiToolVersionHandling == RequireExactVersions::YES;
            // forcing system search also disables version detection
            const bool ignore_version = env_force_system_binaries;

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
                auto paths_from_path = fs.find_from_PATH(tool.system_exe_stem());
                candidate_paths.insert(candidate_paths.end(), paths_from_path.cbegin(), paths_from_path.cend());

                tool.add_system_paths(candidate_paths);
            }

            if (ignore_version)
            {
                // If we are forcing the system copy (and therefore ignoring versions), take the first entry that
                // exists.
                const auto it = std::find_if(candidate_paths.begin(), candidate_paths.end(), [this](const Path& p) {
                    return this->fs.exists(p, IgnoreErrors{});
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
                    tool, candidate_paths, [&min_version, exact_version](const std::array<int, 3>& actual_version) {
                        if (exact_version)
                        {
                            return actual_version[0] == min_version[0] && actual_version[1] == min_version[1] &&
                                   actual_version[2] == min_version[2];
                        }
                        return actual_version[0] > min_version[0] ||
                               (actual_version[0] == min_version[0] && actual_version[1] > min_version[1]) ||
                               (actual_version[0] == min_version[0] && actual_version[1] == min_version[1] &&
                                actual_version[2] >= min_version[2]);
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
                    auto downloaded_path = download_tool(tool.tool_data_name(), *tool_data);
                    auto downloaded_version = tool.get_version(*this, downloaded_path).value_or_exit(VCPKG_LINE_INFO);
                    return {std::move(downloaded_path), std::move(downloaded_version)};
                }
            }

            // If no acceptable tool was found and downloading was unavailable, emit an error message
            LocalizedString s = msg::format(msg::msgErrorMessage);
            s.append(msgToolFetchFailed, msg::tool_name = tool.tool_data_name());
            if (env_force_system_binaries && download_available)
            {
                s.append_raw(" ").append(msgDownloadAvailable, msg::env_var = s_env_vcpkg_force_system_binaries);
            }
            if (consider_system)
            {
                tool.add_system_package_info(s);
            }
            else if (!download_available)
            {
                s.append_raw(" ").append(msgUnknownTool);
            }
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, s);
        }

        const PathAndVersion& get_tool_pathversion(StringView tool) const
        {
            return path_version_cache.get_lazy(tool, [&]() -> PathAndVersion {
                // First deal with specially handled tools.
                // For these we may look in locations like Program Files, the PATH etc as well as the auto-downloaded
                // location.
                if (tool == Tools::CMAKE) return get_path(CMakeProvider());
                if (tool == Tools::GIT) return get_path(GitProvider());
                if (tool == Tools::NINJA) return get_path(NinjaProvider());
                if (tool == Tools::POWERSHELL_CORE) return get_path(PowerShellCoreProvider());
                if (tool == Tools::NUGET) return get_path(NuGetProvider());
                if (tool == Tools::ARIA2) return get_path(Aria2Provider());
                if (tool == Tools::NODE) return get_path(NodeProvider());
                if (tool == Tools::IFW_INSTALLER_BASE) return get_path(IfwInstallerBaseProvider());
                if (tool == Tools::MONO) return get_path(MonoProvider());
                if (tool == Tools::GSUTIL) return get_path(GsutilProvider());
                if (tool == Tools::AWSCLI) return get_path(AwsCliProvider());
                if (tool == Tools::COSCLI) return get_path(CosCliProvider());
                if (tool == Tools::TAR)
                {
                    return {find_system_tar(fs).value_or_exit(VCPKG_LINE_INFO), {}};
                }
                GenericToolProvider provider{tool};
                return get_path(provider);
            });
        }

        virtual const std::string& get_tool_version(StringView tool) const override
        {
            return get_tool_pathversion(tool).version;
        }
    };

    ExpectedL<Path> find_system_tar(const Filesystem& fs)
    {
        const auto tools = fs.find_from_PATH(Tools::TAR);
        if (tools.empty())
        {
            return msg::format(msg::msgErrorMessage)
                .append(msgToolFetchFailed, msg::tool_name = Tools::TAR)
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

    std::unique_ptr<ToolCache> get_tool_cache(Filesystem& fs,
                                              const DownloadManager& downloader,
                                              Path downloads,
                                              Path xml_config,
                                              Path tools,
                                              RequireExactVersions abiToolVersionHandling)
    {
        return std::make_unique<ToolCacheImpl>(fs, downloader, downloads, xml_config, tools, abiToolVersionHandling);
    }
}
