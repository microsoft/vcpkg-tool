#include <vcpkg/base/checks.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    DECLARE_AND_REGISTER_MESSAGE(VcpkgToolsXmlIllFormed,
                                 (),
                                 "",
                                 "Could not parse vcpkgTools.xml. Check your vcpkg installation.");

    DECLARE_AND_REGISTER_MESSAGE(
        VcpkgToolsMissingTool,
        (msg::tool, msg::system_name),
        "",
        "Could not automatically acquire {tool} because there is no entry in {tool} for os={system_name}. You "
        "may be able to install {tool} via your system package manager");

    DECLARE_AND_REGISTER_MESSAGE(
        UnacquirableToolNotFound,
        (msg::tool, msg::version),
        "",
        "A suitable version of {tool} was not found (required v{version}) and unable to automatically "
        "download a portable one. Please install a newer version of {tool}.");

    DECLARE_AND_REGISTER_MESSAGE(
        AcquirableToolNotFound,
        (msg::tool, msg::version),
        "",
        "A suitable version of {tool} was not found (required v{version}). Downloading portable {tool} v{version}...");

    DECLARE_AND_REGISTER_MESSAGE(DownloadingTool, (msg::tool), "", "Downloading {tool}...");
    DECLARE_AND_REGISTER_MESSAGE(DownloadingSourceTarget, (msg::url, msg::path), "", "  {url} -> {path}");
    DECLARE_AND_REGISTER_MESSAGE(ExtractingTool, (msg::tool), "", "Extracting {tool}...");
    DECLARE_AND_REGISTER_MESSAGE(ExpectedExistingTool,
                                 (msg::tool, msg::path),
                                 "",
                                 "Expected {tool} to be at {path} after fetching");

    DECLARE_AND_REGISTER_MESSAGE(ToolUnknown,
                                 (msg::tool),
                                 "Displayed when searching for a tool like 'tar' which is not known to vcpkg.",
                                 "Unknown or unavailable tool: {tool}");

    DECLARE_AND_REGISTER_MESSAGE(
        TarNotFoundWindows,
        (),
        "Displayed when on older or stripped down copies of Windows that don't have a tar.exe in system32.",
        "Could not find tar; the action you tried to take assumes Windows 10 or later which is bundled with tar.exe.");

    DECLARE_AND_REGISTER_MESSAGE(TarNotFoundUnix,
                                 (),
                                 "Displayed when tar is not found on a unix-like system.",
                                 "Could not find tar; please install it from your system package manager.");

    DECLARE_AND_REGISTER_MESSAGE(ToolVersionNotFound,
                                 (msg::path),
                                 "Displayed when invoking a tool to determine its version failed with a nonzero exit "
                                 "code or otherwise invalid version format.",
                                 "Failed to get version of {path}.");

    LocalizedString format_tool_version_not_found(const Path& exe_path, std::string&& output)
    {
        return msg::format(msgToolVersionNotFound, msg::path = exe_path)
            .appendnl()
            .append(LocalizedString::from_string_unchecked(std::move(output)))
            .appendnl();
    }

    DECLARE_AND_REGISTER_MESSAGE(MonoExampleUbuntu,
                                 (),
                                 "",
                                 "Ubuntu 18.04 users may need a newer version of mono, available at "
                                 "https://www.mono-project.com/download/stable/");

    DECLARE_AND_REGISTER_MESSAGE(
        MonoNotFoundUnix,
        (msg::path),
        "Displayed when attempting to find mono failed.",
        "Failed to get version of {path}\nThis may be caused by an incomplete mono installation. Full mono is "
        "available on some systems via `sudo apt install mono-complete`. Ubuntu 18.04 users may "
        "need a newer version of mono, available at https://www.mono-project.com/download/stable/");

    DECLARE_AND_REGISTER_MESSAGE(ToolVersionIllFormedCommand,
                                 (msg::path, msg::command_line),
                                 "Displayed when parsing a tool's output to determine the version failed.",
                                 "Unexpected output of {path} {command_line}.");

    LocalizedString format_tool_version_ill_formed(const Path& exe_path, StringView command_line, std::string&& output)
    {
        return msg::format(msgToolVersionIllFormedCommand, msg::path = exe_path, msg::command_line = command_line)
            .appendnl()
            .append(LocalizedString::from_string_unchecked(std::move(output)))
            .appendnl();
    }
}

namespace vcpkg
{
    struct ToolData
    {
        DotVersion version;
        Path exe_path;
        std::string url;
        Path download_path;
        bool is_archive;
        Path tool_dir_path;
        std::string sha512;
    };

    static ExpectedL<ToolData> parse_tool_data_from_xml(const VcpkgPaths& paths, StringView tool)
    {
#if defined(_WIN32)
        static constexpr StringLiteral OS_STRING = "windows";
#elif defined(__APPLE__)
        static constexpr StringLiteral OS_STRING = "osx";
#elif defined(__linux__)
        static constexpr StringLiteral OS_STRING = "linux";
#elif defined(__FreeBSD__)
        static constexpr StringLiteral OS_STRING = "freebsd";
#elif defined(__OpenBSD__)
        static constexpr StringLiteral OS_STRING = "openbsd";
#else
        return std::string("operating system is unknown");
#endif

#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
        static const auto XML_PATH = paths.scripts / "vcpkgTools.xml";
        static const std::regex XML_VERSION_REGEX{R"###(<tools[\s]+version="([^"]+)">)###"};
        static const std::string XML = paths.get_filesystem().read_contents(XML_PATH, VCPKG_LINE_INFO);
        std::smatch match_xml_version;
        const bool has_xml_version = std::regex_search(XML.cbegin(), XML.cend(), match_xml_version, XML_VERSION_REGEX);
        Checks::check_exit(VCPKG_LINE_INFO, has_xml_version && "2" == match_xml_version[1], msgVcpkgToolsXmlIllFormed);

        const std::regex tool_regex{fmt::format(R"###(<tool[\s]+name="{}"[\s]+os="{}">)###", tool, OS_STRING)};
        std::smatch match_tool_entry;
        const bool has_tool_entry = std::regex_search(XML.cbegin(), XML.cend(), match_tool_entry, tool_regex);
        if (!has_tool_entry)
        {
            auto error = msg::format(msgVcpkgToolsMissingTool, msg::tool = tool, msg::system_name = OS_STRING);
            if (tool == Tools::MONO)
            {
#if defined(__APPLE__)
                error.append(LocalizedString::from_string_unchecked(" (brew install mono)"));
#else
                error.append(LocalizedString::from_string_unchecked(" (sudo apt install mono-complete)"));
                error.appendnl();
                error.append(msg::format(msgMonoExampleUbuntu));
#endif
            }

            return error;
        }

        const std::string tool_data =
            Strings::find_exactly_one_enclosed(XML, match_tool_entry[0].str(), "</tool>").to_string();
        const auto version_as_string = Strings::find_exactly_one_enclosed(tool_data, "<version>", "</version>");
        const auto exe_relative_path =
            Strings::find_exactly_one_enclosed(tool_data, "<exeRelativePath>", "</exeRelativePath>");
        const auto url = Strings::find_exactly_one_enclosed(tool_data, "<url>", "</url>");
        const auto sha512 = Strings::find_exactly_one_enclosed(tool_data, "<sha512>", "</sha512>");
        auto archive_name = Strings::find_at_most_one_enclosed(tool_data, "<archiveName>", "</archiveName>");

        auto version = DotVersion::try_parse_relaxed_lz(version_as_string);
        if (!version.has_value())
        {
            return version.error();
        }

        const std::string tool_dir_name = Strings::concat(tool, "-", version_as_string, "-", OS_STRING);
        const auto tool_dir_path = paths.tools / tool_dir_name;
        const auto exe_path = tool_dir_path / exe_relative_path;
        Path download_path;
        if (auto a = archive_name.get())
        {
            download_path = paths.downloads / a->to_string();
        }
        else
        {
            download_path = paths.downloads / Strings::concat(sha512.substr(0, 8), '-', exe_relative_path);
        }

        return ToolData{version.value_or_exit(VCPKG_LINE_INFO),
                        exe_path,
                        url.to_string(),
                        download_path,
                        archive_name.has_value(),
                        tool_dir_path,
                        sha512.to_string()};
#endif
    }

    struct ToolProvider
    {
        virtual StringLiteral tool_data_name() const = 0;
        virtual StringLiteral exe_stem() const = 0;
        virtual DotVersion default_min_version() const = 0;

        virtual void add_special_paths(std::vector<Path>& out_candidate_paths) const { (void)out_candidate_paths; }
        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths& paths, const Path& exe_path) const = 0;
    };

    template<typename Func>
    static ExpectedL<PathAndVersion> find_first_with_sufficient_version(const VcpkgPaths& paths,
                                                                        const ToolProvider& tool_provider,
                                                                        const std::vector<Path>& candidates,
                                                                        Func&& accept_version)
    {
        std::vector<LocalizedString> errors;
        const auto& fs = paths.get_filesystem();
        for (auto&& candidate : candidates)
        {
            if (!fs.exists(candidate, IgnoreErrors{})) continue;
            auto maybe_version = tool_provider.get_version(paths, candidate);
            if (const auto version = maybe_version.get())
            {
                if (accept_version(*version))
                {
                    return PathAndVersion{candidate, *version};
                }
            }
            else
            {
                errors.push_back(std::move(maybe_version).error());
            }
        }

        return join_newlines(errors);
    }

    static Path fetch_tool(const VcpkgPaths& paths, StringView tool_name, const ToolData& tool_data)
    {
        const std::string& version_as_string = tool_data.version.version_string;
        Checks::check_exit(VCPKG_LINE_INFO,
                           !tool_data.url.empty(),
                           msgUnacquirableToolNotFound,
                           msg::tool = tool_name,
                           msg::version = version_as_string);

        msg::println(msgAcquirableToolNotFound, msg::tool = tool_name, msg::version = version_as_string);
        auto& fs = paths.get_filesystem();
        if (!fs.exists(tool_data.download_path, IgnoreErrors{}))
        {
            msg::println(msgDownloadingTool, msg::tool = tool_name);
            msg::println(msgDownloadingSourceTarget, msg::url = tool_data.url, msg::path = tool_data.download_path);
            paths.get_download_manager().download_file(fs, tool_data.url, tool_data.download_path, tool_data.sha512);
        }
        else
        {
            verify_downloaded_file_hash(fs, tool_data.url, tool_data.download_path, tool_data.sha512);
        }

        if (tool_data.is_archive)
        {
            msg::println(msgExtractingTool, msg::tool = tool_name);
            extract_archive(paths, tool_data.download_path, tool_data.tool_dir_path);
        }
        else
        {
            fs.create_directories(tool_data.exe_path.parent_path(), IgnoreErrors{});
            fs.rename(tool_data.download_path, tool_data.exe_path, IgnoreErrors{});
        }

        Checks::check_exit(VCPKG_LINE_INFO,
                           fs.exists(tool_data.exe_path, IgnoreErrors{}),
                           msgExpectedExistingTool,
                           msg::tool = tool_name,
                           msg::path = tool_data.exe_path);

        return tool_data.exe_path;
    }

    static PathAndVersion fetch_tool(const VcpkgPaths& paths,
                                     const ToolProvider& tool_provider,
                                     const ToolData& tool_data)
    {
        auto downloaded_path = fetch_tool(paths, tool_provider.tool_data_name(), tool_data);
        auto downloaded_version = tool_provider.get_version(paths, downloaded_path).value_or_exit(VCPKG_LINE_INFO);
        return {std::move(downloaded_path), std::move(downloaded_version)};
    }

    static ExpectedL<PathAndVersion> get_path(const VcpkgPaths& paths,
                                              const ToolProvider& tool,
                                              bool exact_version = false)
    {
        auto& fs = paths.get_filesystem();

        auto min_version = tool.default_min_version();

        std::vector<Path> candidate_paths;
        auto maybe_tool_data = parse_tool_data_from_xml(paths, tool.tool_data_name());
        if (auto tool_data = maybe_tool_data.get())
        {
            candidate_paths.push_back(tool_data->exe_path);
            min_version = tool_data->version;
        }

        auto exe_stem = tool.exe_stem();
        if (!exe_stem.empty())
        {
            auto paths_from_path = fs.find_from_PATH(exe_stem);
            candidate_paths.insert(candidate_paths.end(), paths_from_path.cbegin(), paths_from_path.cend());
        }

        tool.add_special_paths(candidate_paths);

        const auto maybe_path = find_first_with_sufficient_version(
            paths, tool, candidate_paths, [&min_version, exact_version](const DotVersion& actual_version) {
                if (exact_version)
                {
                    return min_version == actual_version;
                }

                return min_version <= actual_version;
            });

        if (const auto p = maybe_path.get())
        {
            return *p;
        }

        if (auto tool_data = maybe_tool_data.get())
        {
            return fetch_tool(paths, tool, *tool_data);
        }

        auto error = maybe_path.error();
        error.appendnl().append(maybe_tool_data.error());
        Checks::exit_maybe_upgrade(VCPKG_LINE_INFO, error);
    }

    struct CMakeProvider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return Tools::CMAKE; }
        virtual StringLiteral exe_stem() const override { return Tools::CMAKE; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(3, 17, 1); }

        virtual void add_special_paths(std::vector<Path>& out_candidate_paths) const override
        {
#if defined(_WIN32)
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get()) out_candidate_paths.push_back(*pf / "CMake" / "bin" / "cmake.exe");
            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get())
                out_candidate_paths.push_back(*pf / "CMake" / "bin" / "cmake.exe");
#else
            // TODO: figure out if this should do anything on non-Windows
            (void)out_candidate_paths;
#endif
        }
        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return format_tool_version_not_found(exe_path, std::move(rc.output));
            }

            /* Sample output:
cmake version 3.10.2

CMake suite maintained and supported by Kitware (kitware.com/cmake).
                */

            // There are two expected output formats to handle: "cmake3 version x.x.x" and "cmake version x.x.x"
            auto simplifiedOutput = Strings::replace_all(rc.output, "cmake3", "cmake");
            return DotVersion::try_parse_relaxed(
                Strings::find_exactly_one_enclosed(simplifiedOutput, "cmake version ", "\n"));
        }
    };

    struct NinjaProvider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return Tools::NINJA; }
        virtual StringLiteral exe_stem() const override { return Tools::NINJA; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(3, 5, 1); }

        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return format_tool_version_not_found(exe_path, std::move(rc.output));
            }

            /* Sample output:
1.8.2
                */
            return DotVersion::try_parse_relaxed(Strings::trim(rc.output));
        }
    };

    struct NuGetProvider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return Tools::NUGET; }
        virtual StringLiteral exe_stem() const override { return Tools::NUGET; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(4, 6, 2); }

        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths& paths, const Path& exe_path) const override
        {
            Command cmd;
#ifndef _WIN32
            cmd.path_arg(paths.get_required_tool_exe(Tools::MONO));
#else
            (void)paths;
#endif
            cmd.path_arg(exe_path);
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
#ifndef _WIN32
                return msg::format(msgMonoNotFoundUnix, msg::path = exe_path)
                    .appendnl()
                    .append(LocalizedString::from_string_unchecked(std::move(rc.output)))
                    .appendnl();
#else
                return format_tool_version_not_found(exe_path, std::move(rc.output));
#endif
            }

            /* Sample output:
NuGet Version: 4.6.2.5055
usage: NuGet <command> [args] [options]
Type 'NuGet help <command>' for help on a specific command.

[[[List of available commands follows]]]
                */
            return DotVersion::try_parse_relaxed(
                Strings::trim(Strings::find_exactly_one_enclosed(rc.output, "NuGet Version: ", "\n")));
        }
    };

    struct Aria2Provider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return Tools::ARIA2; }
        virtual StringLiteral exe_stem() const override { return "aria2c"; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(1, 33, 1); }
        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return format_tool_version_not_found(exe_path, std::move(rc.output));
            }

            /* Sample output:
aria2 version 1.35.0
Copyright (C) 2006, 2019 Tatsuhiro Tsujikawa
[...]
                */
            const auto idx = rc.output.find("aria2 version ");
            if (idx == std::string::npos)
            {
                return format_tool_version_ill_formed(exe_path, "--version", std::move(rc.output));
            }

            auto start = rc.output.data() + idx + 14;
            char newlines[] = "\r\n";
            auto end = std::find_first_of(start, rc.output.data() + rc.output.size(), &newlines[0], &newlines[2]);
            return DotVersion::try_parse_relaxed(StringView{start, end});
        }
    };

    struct NodeProvider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return Tools::NODE; }
        virtual StringLiteral exe_stem() const override { return Tools::NODE; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(16, 12, 0); }

        virtual void add_special_paths(std::vector<Path>& out_candidate_paths) const override
        {
#if defined(_WIN32)
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get()) out_candidate_paths.push_back(*pf / "nodejs" / "node.exe");
            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get()) out_candidate_paths.push_back(*pf / "nodejs" / "node.exe");
#else
            // TODO: figure out if this should do anything on non-windows
            (void)out_candidate_paths;
#endif
        }

        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return format_tool_version_not_found(exe_path, std::move(rc.output));
            }

            // Sample output: v16.12.0
            auto start = rc.output.data();
            if (rc.output.empty() || *start != 'v')
            {
                return format_tool_version_ill_formed(exe_path, "--version", std::move(rc.output));
            }

            ++start;
            char newlines[] = "\r\n";
            auto end = std::find_first_of(start, rc.output.data() + rc.output.size(), &newlines[0], &newlines[2]);
            return DotVersion::try_parse_relaxed(StringView{start, end});
        }
    };

    ExpectedL<DotVersion> parse_git_version(StringView git_version)
    {
        /* Sample output:
git version 2.17.1.windows.2

Expected result: 2.17.1.2
    */
        constexpr StringLiteral prefix = "git version ";
        auto first = git_version.begin();
        const auto last = git_version.end();

        first = std::search(first, last, prefix.begin(), prefix.end());
        if (first != last)
        {
            first += prefix.size();
        }

        std::string accumulator;
        for (; first != last; ++first)
        {
            if (*first == '.')
            {
                if (accumulator.empty() || accumulator.back() != '.')
                {
                    accumulator.push_back(*first);
                }
            }
            else if (Parse::ParserBase::is_ascii_digit(*first))
            {
                accumulator.push_back(*first);
            }
        }

        return DotVersion::try_parse_relaxed(accumulator);
    }

    struct GitProvider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return Tools::GIT; }
        virtual StringLiteral exe_stem() const override { return Tools::GIT; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(2, 7, 4); }

        virtual void add_special_paths(std::vector<Path>& out_candidate_paths) const override
        {
#if defined(_WIN32)
            const auto& program_files = get_program_files_platform_bitness();
            if (const auto pf = program_files.get()) out_candidate_paths.push_back(*pf / "git" / "cmd" / "git.exe");
            const auto& program_files_32_bit = get_program_files_32_bit();
            if (const auto pf = program_files_32_bit.get())
                out_candidate_paths.push_back(*pf / "git" / "cmd" / "git.exe");
#else
            // TODO: figure out if this should do anything on non-windows
            (void)out_candidate_paths;
#endif
        }

        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return format_tool_version_not_found(exe_path, std::move(rc.output));
            }

            return parse_git_version(rc.output);
        }
    };

    struct MonoProvider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return Tools::MONO; }
        virtual StringLiteral exe_stem() const override { return Tools::MONO; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(0, 0, 0); }

        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths&, const Path& exe_path) const override
        {
            auto rc = cmd_execute_and_capture_output(Command(exe_path).string_arg("--version"));
            if (rc.exit_code != 0)
            {
                return format_tool_version_not_found(exe_path, std::move(rc.output));
            }

            /* Sample output:
Mono JIT compiler version 6.8.0.105 (Debian 6.8.0.105+dfsg-2 Wed Feb 26 23:23:50 UTC 2020)
                */
            const auto idx = rc.output.find("Mono JIT compiler version ");
            if (idx == std::string::npos)
            {
                return format_tool_version_ill_formed(exe_path, "--version", std::move(rc.output));
            }

            return DotVersion::try_parse_relaxed(rc.output.substr(idx));
        }
    };

    struct GsutilProvider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return Tools::GSUTIL; }
        virtual StringLiteral exe_stem() const override { return Tools::GSUTIL; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(4, 56, 0); }

        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return format_tool_version_not_found(exe_path, std::move(rc.output));
            }

            /* Sample output:
gsutil version: 4.58
                */

            const auto idx = rc.output.find("gsutil version: ");
            if (idx == std::string::npos)
            {
                return format_tool_version_ill_formed(exe_path, "version", std::move(rc.output));
            }

            return DotVersion::try_parse_relaxed(rc.output.substr(idx));
        }
    };

    struct AwsCliProvider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return Tools::AWSCLI; }
        virtual StringLiteral exe_stem() const override { return Tools::AWSCLI; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(2, 4, 4); }

        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return format_tool_version_not_found(exe_path, std::move(rc.output));
            }

            /* Sample output:
aws-cli/2.4.4 Python/3.8.8 Windows/10 exe/AMD64 prompt/off
                */

            const auto idx = rc.output.find("aws-cli/");
            if (idx == std::string::npos)
            {
                return format_tool_version_ill_formed(exe_path, "--version", std::move(rc.output));
            }

            return DotVersion::try_parse_relaxed(rc.output.substr(idx));
        }
    };

    struct IfwInstallerBaseProvider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return "installerbase"; }
        virtual StringLiteral exe_stem() const override { return ""; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(0, 0, 0); }

        virtual void add_special_paths(std::vector<Path>& out_candidate_paths) const override
        {
            (void)out_candidate_paths;
            // TODO: Uncomment later
            // const std::vector<path> from_path = find_from_PATH("installerbase");
            // candidate_paths.insert(candidate_paths.end(), from_path.cbegin(), from_path.cend());
            // candidate_paths.push_back(path(get_environment_variable("HOMEDRIVE").value_or("C:")) /
            // "Qt" / "Tools" / "QtInstallerFramework" / "3.1" / "bin" / "installerbase.exe");
            // candidate_paths.push_back(path(get_environment_variable("HOMEDRIVE").value_or("C:")) /
            // "Qt" / "QtIFW-3.1.0" / "bin" / "installerbase.exe");
        }

        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths&, const Path& exe_path) const override
        {
            auto cmd = Command(exe_path).string_arg("--framework-version");
            auto rc = cmd_execute_and_capture_output(cmd);
            if (rc.exit_code != 0)
            {
                return format_tool_version_not_found(exe_path, std::move(rc.output));
            }

            /* Sample output:
3.1.81
                */
            return DotVersion::try_parse_relaxed(std::move(rc.output));
        }
    };

    struct PowerShellCoreProvider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return "powershell-core"; }
        virtual StringLiteral exe_stem() const override { return "pwsh"; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(7, 0, 3); }

        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths&, const Path& exe_path) const override
        {
            auto rc = cmd_execute_and_capture_output(Command(exe_path).string_arg("--version"));
            if (rc.exit_code != 0)
            {
                return format_tool_version_not_found(exe_path, std::move(rc.output));
            }

            // Sample output: PowerShell 7.0.3\r\n
            auto& output = rc.output;
            if (!Strings::starts_with(output, "PowerShell "))
            {
                return format_tool_version_ill_formed(exe_path, "--version", std::move(output));
            }

            output.erase(0, 11);
            return DotVersion::try_parse_relaxed(Strings::trim(std::move(output)));
        }
    };

    struct TarWindowsProvider : ToolProvider
    {
        virtual StringLiteral tool_data_name() const override { return Tools::TAR; }
        virtual StringLiteral exe_stem() const override { return Tools::TAR; }
        virtual DotVersion default_min_version() const override { return DotVersion::from_values(3, 5, 2); }
        virtual ExpectedL<DotVersion> get_version(const VcpkgPaths&, const Path& exe_path) const override
        {
            auto rc = cmd_execute_and_capture_output(Command(exe_path).string_arg("--version"));
            if (rc.exit_code != 0)
            {
                return format_tool_version_not_found(exe_path, std::move(rc.output));
            }

            // Sample output: bsdtar 3.5.2 - libarchive 3.5.2 zlib/1.2.5.f-ipp bz2lib/1.0.6
            auto& output = rc.output;
            if (!Strings::starts_with(output, "bsdtar "))
            {
                return format_tool_version_ill_formed(exe_path, "--version", std::move(output));
            }

            auto post_space = output.find(' ', 7);
            if (post_space == std::string::npos)
            {
                return format_tool_version_ill_formed(exe_path, "--version", std::move(output));
            }

            return DotVersion::try_parse_relaxed(StringView{output}.substr(7, post_space - 7));
        }
    };

    ToolCache::~ToolCache() = default;

    struct ToolCacheImpl final : ToolCache
    {
        RequireExactVersions abiToolVersionHandling;
        vcpkg::Cache<std::string, ExpectedL<Path>> system_cache;
        vcpkg::Cache<std::string, ExpectedL<Path>> path_only_cache;
        vcpkg::Cache<std::string, ExpectedL<PathAndVersion>> path_version_cache;

        ToolCacheImpl(RequireExactVersions abiToolVersionHandling) : abiToolVersionHandling(abiToolVersionHandling) { }

        virtual const ExpectedL<Path>& get_tool_path_from_system(const Filesystem& fs, StringView tool) const override
        {
            return system_cache.get_lazy(tool, [&]() -> ExpectedL<Path> {
                if (tool == Tools::TAR)
                {
                    auto tars = fs.find_from_PATH("tar");
                    if (tars.empty())
                    {
#if defined(_WIN32)
                        return msg::format(msgTarNotFoundWindows);
#else  // ^^^ _WIN32 // !_WIN32 vvv
                        return msg::format(msgTarNotFoundUnix);
#endif // ^^^ !_WIN32
                    }

                    return std::move(tars[0]);
                }

                return msg::format(msgToolUnknown, msg::tool = tool);
            });
        }

        virtual const ExpectedL<Path>& get_tool_path(const VcpkgPaths& paths, StringView tool) const override
        {
            return path_only_cache.get_lazy(tool, [&]() -> ExpectedL<Path> {
                if (tool == Tools::IFW_BINARYCREATOR)
                {
                    return get_tool_path(paths, Tools::IFW_INSTALLER_BASE).map([](Path installer_base) {
                        installer_base.replace_filename("binarycreator.exe");
                        return installer_base;
                    });
                }

                if (tool == Tools::IFW_REPOGEN)
                {
                    return get_tool_path(paths, Tools::IFW_INSTALLER_BASE).map([](Path installer_base) {
                        installer_base.replace_filename("repogen.exe");
                        return installer_base;
                    });
                }

                return get_tool_versioned(paths, tool).map([](const PathAndVersion& pv) { return pv.path; });
            });
        }

        virtual const ExpectedL<PathAndVersion>& get_tool_versioned(const VcpkgPaths& paths,
                                                                    StringView tool) const override
        {
            return path_version_cache.get_lazy(tool, [&]() -> ExpectedL<PathAndVersion> {
                // First deal with specially handled tools.
                // For these we may look in locations like Program Files, the PATH etc as well as the auto-downloaded
                // location.
                if (tool == Tools::CMAKE)
                {
                    if (get_environment_variable("VCPKG_FORCE_SYSTEM_BINARIES").has_value())
                    {
                        return PathAndVersion{"cmake", DotVersion::from_values(0, 0, 0)};
                    }

                    return get_path(paths, CMakeProvider(), abiToolVersionHandling == RequireExactVersions::YES);
                }
                if (tool == Tools::GIT)
                {
                    if (get_environment_variable("VCPKG_FORCE_SYSTEM_BINARIES").has_value())
                    {
                        return PathAndVersion{"git", DotVersion::from_values(0, 0, 0)};
                    }

                    return get_path(paths, GitProvider());
                }
                if (tool == Tools::NINJA)
                {
                    if (get_environment_variable("VCPKG_FORCE_SYSTEM_BINARIES").has_value())
                    {
                        return PathAndVersion{"ninja", DotVersion::from_values(0, 0, 0)};
                    }

                    return get_path(paths, NinjaProvider());
                }
                if (tool == Tools::POWERSHELL_CORE)
                {
                    if (get_environment_variable("VCPKG_FORCE_SYSTEM_BINARIES").has_value())
                    {
                        return PathAndVersion{"pwsh", DotVersion::from_values(0, 0, 0)};
                    }

                    return get_path(
                        paths, PowerShellCoreProvider(), abiToolVersionHandling == RequireExactVersions::YES);
                }
                if (tool == Tools::NUGET) return get_path(paths, NuGetProvider());
                if (tool == Tools::ARIA2) return get_path(paths, Aria2Provider());
                if (tool == Tools::NODE) return get_path(paths, NodeProvider());
                if (tool == Tools::IFW_INSTALLER_BASE) return get_path(paths, IfwInstallerBaseProvider());
                if (tool == Tools::MONO) return get_path(paths, MonoProvider());
                if (tool == Tools::GSUTIL)
                {
                    if (get_environment_variable("VCPKG_FORCE_SYSTEM_BINARIES").has_value())
                    {
                        return PathAndVersion{"gsutil", DotVersion::from_values(0, 0, 0)};
                    }

                    return get_path(paths, GsutilProvider());
                }
                if (tool == Tools::AWSCLI)
                {
                    if (get_environment_variable("VCPKG_FORCE_SYSTEM_BINARIES").has_value())
                    {
                        return PathAndVersion{"aws", DotVersion::from_values(0, 0, 0)};
                    }

                    return get_path(paths, AwsCliProvider());
                }
                if (tool == Tools::TAR)
                {
#if defined(_WIN32)
                    return get_path(paths, TarWindowsProvider());
#else  // ^^^ _WIN32 // !_WIN32 vvv
                    auto tars = paths.get_filesystem().find_from_PATH("tar");
                    if (tars.empty())
                    {
                        return msg::format(msgTarNotFoundUnix);
                    }

                    return PathAndVersion{tars[0], DotVersion::from_values(0, 0, 0)};
#endif // ^^^ !_WIN32
                }

                // For other tools, we simply always auto-download them.
                auto maybe_tool_data = parse_tool_data_from_xml(paths, tool);
                if (auto p_tool_data = maybe_tool_data.get())
                {
                    if (paths.get_filesystem().exists(p_tool_data->exe_path, IgnoreErrors{}))
                    {
                        return PathAndVersion{p_tool_data->exe_path, p_tool_data->version};
                    }

                    return PathAndVersion{fetch_tool(paths, tool, *p_tool_data), p_tool_data->version};
                }

                return maybe_tool_data.error();
            });
        }
    };

    std::unique_ptr<ToolCache> get_tool_cache(RequireExactVersions abiToolVersionHandling)
    {
        return std::make_unique<ToolCacheImpl>(abiToolVersionHandling);
    }
}
