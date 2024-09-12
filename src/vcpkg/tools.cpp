#include <vcpkg/base/cache.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
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

#include <regex>

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

    struct ArchToolData
    {
        StringView tool;
        StringView os;
        Optional<CPUArchitecture> arch;
        StringView version;
        StringView exeRelativePath;
        StringView url;
        StringView sha512;
        StringView archiveName;
    };

    static Optional<ArchToolData&> get_raw_tool_data(StringView toolname, CPUArchitecture arch, StringView os)
    {
        static std::vector<ArchToolData> tool_data = {
            {
                "python3",
                "windows",
                nullopt,
                "3.11.8",
                "python.exe",
                "https://www.python.org/ftp/python/3.11.8/python-3.11.8-embed-win32.zip",
                "c88ef02f0860000dbc59361cfe051e3e8dc7d208ed39bb5bc20a3e8b8711b578926e281a11941787ea61b2ef05b945ab313332"
                "2dcb85b916f79ac4ada57f6309",
                "python-3.11.8-embed-win32.zip",
            },
            {
                "python3_with_venv",
                "windows",
                nullopt,
                "3.11.8",
                "tools/python.exe",
                "https://www.nuget.org/api/v2/package/python/3.11.8",
                "8c1abd622fb2795fb87ab5208764fe13f7f4d69f2283b4237ea47b7c14b0989b5b4169f1ac1d3b1f417c6c2cc3d85ce151370d"
                "ae8efc807c93e57cd67fa3c8cf",
                "python-3.11.8.nupkg.zip",
            },
            {
                "cmake",
                "windows",
                nullopt,
                "3.29.2",
                "cmake-3.29.2-windows-i386/bin/cmake.exe",
                "https://github.com/Kitware/CMake/releases/download/v3.29.2/cmake-3.29.2-windows-i386.zip",
                "562de7b577c99fe347b00437d14ce375a8e5a60504909cb67d2f73c372d39a2f76d2b42b69e4aeb31a4879e1bcf6f7c2d41f2a"
                "ce12180ea83ba7af48879d40ab",
                "cmake-3.29.2-windows-i386.zip",
            },
            {
                "cmake",
                "osx",
                nullopt,
                "3.29.2",
                "cmake-3.29.2-macos-universal/CMake.app/Contents/bin/cmake",
                "https://github.com/Kitware/CMake/releases/download/v3.29.2/cmake-3.29.2-macos-universal.tar.gz",
                "26aab0163965f3d560dfd6b1f72c5e77192338237ebe286099fd62f243f1bbd4857b9193118386b801c00dc5cfbc5bc8af8481"
                "4692fcfadcf56c7d2faab52533",
                "cmake-3.29.2-macos-universal.tar.gz",
            },
            {
                "cmake",
                "linux",
                CPUArchitecture::ARM64,
                "3.29.2",
                "cmake-3.29.2-linux-aarch64/bin/cmake",
                "https://github.com/Kitware/CMake/releases/download/v3.29.2/cmake-3.29.2-linux-aarch64.tar.gz",
                "206b00604caf72f3dc644c7a5ac6a335814520fbf1512b9f2d4a5e8c26025d286bd106d2925ffbc874c754c518cbdb07f16919"
                "16c39fcfd1202a47f592c8e3b0",
                "cmake-3.29.2-linux-aarch64.tar.gz",
            },
            {
                "cmake",
                "linux",
                nullopt,
                "3.29.2",
                "cmake-3.29.2-linux-x86_64/bin/cmake",
                "https://github.com/Kitware/CMake/releases/download/v3.29.2/cmake-3.29.2-linux-x86_64.tar.gz",
                "d88082d582f1774a3d89efbf3f31a03b08a663c359d603d87ee7c4afd862d4164d2b0b9f0b32cece8efee3acfa86b44214cd4f"
                "7606b99b8334183a05d5f06edc",
                "cmake-3.29.2-linux-x86_64.tar.gz",
            },
            {
                "git",
                "windows",
                nullopt,
                "2.7.4",
                "mingw64/bin/git.exe",
                "https://github.com/git-for-windows/git/releases/download/v2.43.0.windows.1/"
                "PortableGit-2.43.0-64-bit.7z.exe",
                "02ec40f55a6de18f305530e9415b6ce0a597359ebb9a58cf727ac84eceb0003b0f437941b76872b6568157a333c8e6e8d865a3"
                "6faf874fd5f04774deb6a9387a",
                "PortableGit-2.43.0-32-bit.7z.exe",
            },
            {
                "git",
                "linux",
                nullopt,
                "2.7.4",
                "",
                "",
                "",
            },
            {
                "git",
                "osx",
                nullopt,
                "2.7.4",
                "",
                "",
                "",
            },
            {
                "git",
                "freebsd",
                nullopt,
                "2.7.4",
                "",
                "",
                "",
            },
            {
                "gsutil",
                "windows",
                nullopt,
                "4.65",
                "google-cloud-sdk/bin/gsutil.cmd",
                "https://dl.google.com/dl/cloudsdk/channels/rapid/downloads/"
                "google-cloud-sdk-347.0.0-windows-x86_64-bundled-python.zip",
                "e2792e17b132aad77f7c0b9fd26faf415e9437923d9227a9e6d253554e6843d29a6ddad0a7fb5e9aea4a130fd4c521e6ece884"
                "4fd4a4f9e8d580348775425389",
                "google-cloud-sdk-347.0.0-windows-x86_64-bundled-python.zip",
            },
            {
                "gsutil",
                "osx",
                nullopt,
                "4.65",
                "gsutil/gsutil",
                "https://storage.googleapis.com/pub/gsutil_4.65.tar.gz",
                "2c5c9dea48147f97180a491bbb9e24e8cbcd4f3452620e2f80338b781e4dfc90bb754e3bbfa05e1b990e44bff52d990d8c2dd5"
                "1bc83d112339d8e6096a2f21c8",
                "gsutil_4.65.tar.gz",
            },
            {
                "gsutil",
                "linux",
                nullopt,
                "4.65",
                "gsutil/gsutil",
                "https://storage.googleapis.com/pub/gsutil_4.65.tar.gz",
                "2c5c9dea48147f97180a491bbb9e24e8cbcd4f3452620e2f80338b781e4dfc90bb754e3bbfa05e1b990e44bff52d990d8c2dd5"
                "1bc83d112339d8e6096a2f21c8",
                "gsutil_4.65.tar.gz",
            },
            {
                "vswhere",
                "windows",
                nullopt,
                "3.1.7",
                "vswhere.exe",
                "https://github.com/microsoft/vswhere/releases/download/3.1.7/vswhere.exe",
                "40c534eb27f079c15c9782f53f82c12dabfede4d3d85f0edf8a855c2b0d5e12921a96506b37c210beab3c33220f8ff098447ad"
                "054e82d8c2603964975fc12076",
            },
            {
                "nuget",
                "windows",
                nullopt,
                "6.10.0",
                "nuget.exe",
                "https://dist.nuget.org/win-x86-commandline/v6.10.0/nuget.exe",
                "71d7307bb89de2df3811419c561efa00618a4c68e6ce481b0bdfc94c7c6c6d126a54eb26a0015686fabf99f109744ca41fead9"
                "9e97139cdc86dde16a5ec3e7cf",
            },
            {
                "nuget",
                "linux",
                nullopt,
                "6.10.0",
                "nuget.exe",
                "https://dist.nuget.org/win-x86-commandline/v6.10.0/nuget.exe",
                "71d7307bb89de2df3811419c561efa00618a4c68e6ce481b0bdfc94c7c6c6d126a54eb26a0015686fabf99f109744ca41fead9"
                "9e97139cdc86dde16a5ec3e7cf",
            },
            {
                "nuget",
                "osx",
                nullopt,
                "6.10.0",
                "nuget.exe",
                "https://dist.nuget.org/win-x86-commandline/v6.10.0/nuget.exe",
                "71d7307bb89de2df3811419c561efa00618a4c68e6ce481b0bdfc94c7c6c6d126a54eb26a0015686fabf99f109744ca41fead9"
                "9e97139cdc86dde16a5ec3e7cf",
            },
            {
                "coscli",
                "windows",
                nullopt,
                "0.11.0",
                "coscli-windows.exe",
                "https://github.com/tencentyun/coscli/releases/download/v0.11.0-beta/coscli-windows.exe",
                "38a521ec80cdb6944124f4233d7e71eed8cc9f9be2c0c736269915d21c3718ea8131e4516bb6eeada6df331f5fa8f47a299907"
                "e50ee9edbe0114444520974d06",
            },
            {
                "coscli",
                "linux",
                nullopt,
                "0.11.0",
                "coscli-linux",
                "https://github.com/tencentyun/coscli/releases/download/v0.11.0-beta/coscli-linux",
                "9c930a1d308e9581a0e2fdfe3751ea7fe13d6068df90ca6465740ec3eda034202ef71ec54c99e90015ff81aa68aa1489567db5"
                "e411e222eb7258704bdac7e924",
            },
            {
                "coscli",
                "osx",
                nullopt,
                "0.11.0",
                "coscli-mac",
                "https://github.com/tencentyun/coscli/releases/download/v0.11.0-beta/coscli-mac",
                "9556335bfc8bc14bace6dfced45fa77fb07c80f08aa975e047a54efda1d19852aae0ea68a5bc7f04fbd88e3edce5a73512a612"
                "16b1c5ff4cade224de4a9ab8db",
            },
            {
                "installerbase",
                "windows",
                nullopt,
                "4.4.0",
                "QtInstallerFramework-win-x86/bin/installerbase.exe",
                "https://download.qt.io/official_releases/qt-installer-framework/4.4.0/"
                "installer-framework-opensource-src-4.4.0.zip",
                "fc713f54bfe2781cb232cd0ae8eddb96833ec178d53a55ec0b01886aa048b13441eb49a1f33282e8eab7259cfe512c890d50b8"
                "e632d3dbf501a0bf1fd83de947",
                "installer-framework-opensource-src-4.4.0.zip",
            },
            {
                "7zip_msi",
                "windows",
                nullopt,
                "24.08",
                "Files/7-Zip/7z.exe",
                "https://github.com/ip7z/7zip/releases/download/24.08/7z2408-x64.msi",
                "3259bf5e251382333c9d18a3fc01d83491fb41bc4ac4ddb25a02918494594c1074482b6608189a8a89e343d78e34d57420cdef"
                "f1d7ace5acfdcaacc8776f1be8",
                "7z2408-x64.msi",
            },
            {
                "7zip",
                "windows",
                nullopt,
                "24.08",
                "7za.exe",
                "https://github.com/ip7z/7zip/releases/download/24.08/7z2408-extra.7z",
                "35f55236fccfb576ca014e29d0c35f4a213e53f06683bd2e82f869ed02506e230c8dd623c01d0207244d6a997031f737903456"
                "b7ad4a44db1717f0a17a78602e",
                "7z2408-extra.7z",
            },
            {
                "7zr",
                "windows",
                nullopt,
                "24.08",
                "7zr.exe",
                "https://github.com/ip7z/7zip/releases/download/24.08/7zr.exe",
                "424196f2acf5b89807f4038683acc50e7604223fc630245af6bab0e0df923f8b1c49cb09ac709086568c214c3f53dcb7d6c32e"
                "8a54af222a3ff78cfab9c51670",
            },
            {
                "aria2",
                "windows",
                nullopt,
                "1.37.0",
                "aria2-1.37.0-win-64bit-build1/aria2c.exe",
                "https://github.com/aria2/aria2/releases/download/release-1.37.0/aria2-1.37.0-win-64bit-build1.zip",
                "6d78405da9cf5639dbe8174787002161b8124d73880fb57cc8c0a3a63982f84e46df4e626990c58f23452965ad925f0d37cb91"
                "47e99b25c3d7ca0ea49602f34d",
                "aria2-1.37.0-win-64bit-build1.zip",
            },
            {
                "aria2",
                "osx",
                nullopt,
                "1.35.0",
                "aria2-1.35.0/bin/aria2c",
                "https://github.com/aria2/aria2/releases/download/release-1.35.0/aria2-1.35.0-osx-darwin.tar.bz2",
                "3bb32b7d55347d1af37c6f4ebf0e20b38ce51c37a1baf92f7ad1762000539a03413dd679a6d902fdb1805fa71917300c9692ac"
                "eee012eb06ecdff10491137aec",
                "aria2-1.35.0-osx-darwin.tar.bz2",
            },
            {
                "ninja",
                "windows",
                nullopt,
                "1.11.1",
                "ninja.exe",
                "https://github.com/ninja-build/ninja/releases/download/v1.11.1/ninja-win.zip",
                "a700e794c32eb67b9f87040db7f1ba3a8e891636696fc54d416b01661c2421ff46fa517c97fd904adacdf8e621df3e68ea3801"
                "05b909ae8b6651a78ae7eb3199",
                "ninja-win-1.11.1.zip",
            },
            {
                "ninja",
                "linux",
                nullopt,
                "1.11.1",
                "ninja",
                "https://github.com/ninja-build/ninja/releases/download/v1.11.1/ninja-linux.zip",
                "6403dac9196baffcff614fa73ea530752997c8db6bbfbaa0446b4b09d7327e2aa6e8615d1283c961d3bf0df497e85ba8660414"
                "9f1505ee75f89d600245a45dde",
                "ninja-linux-1.11.1.zip",
            },
            {
                "ninja",
                "osx",
                nullopt,
                "1.11.1",
                "ninja",
                "https://github.com/ninja-build/ninja/releases/download/v1.11.1/ninja-mac.zip",
                "dad33b0918c60bbf5107951a936175b1610b4894a408f4ba4b47a2f5b328fc982a52a2aed6a0cb75028ee4765af5083bea6661"
                "1c37516826eb0c851366bb4427",
                "ninja-mac-1.11.1.zip",
            },
            {
                "powershell-core",
                "windows",
                nullopt,
                "7.2.23",
                "pwsh.exe",
                "https://github.com/PowerShell/PowerShell/releases/download/v7.2.23/PowerShell-7.2.23-win-x64.zip",
                "b374a878df02980d54e17ad7cfc9021e331748c3770f586be61356c257494d1b33899c1167d09a35c210bc084474aefdff972f"
                "672d16afb43be0562b3589285a",
                "PowerShell-7.2.23-win-x64.zip",
            },
            {
                "node",
                "windows",
                nullopt,
                "16.15.1",
                "node-v16.15.1-win-x64/node.exe",
                "https://nodejs.org/dist/v16.15.1/node-v16.15.1-win-x64.7z",
                "7ec4bfe2ea6034e1461e306b6372d62c0c5d1060c453ba76a73a5cec38ac26b5952a744caa9071455329caa58eb0a96d26c688"
                "54c8915c17610ff27b0cf2c1cf",
                "node-v16.15.1-win-x64.7z",
            },
            {
                "node",
                "linux",
                nullopt,
                "16.15.1",
                "node-v16.15.1-linux-x64/bin/node",
                "https://nodejs.org/dist/v16.15.1/node-v16.15.1-linux-x64.tar.gz",
                "5ad3b4b9caeaa8d31503efa99f5a593118a267dec9d4181d019732126ba248ce9a901207115b3f6b899eb5b3f0373c7f77ea95"
                "cc92ac625cddf437ee9b8b8919",
                "node-v16.15.1-linux-x64.tar.gz",
            },
            {
                "node",
                "osx",
                nullopt,
                "16.15.1",
                "node-v16.15.1-darwin-x64/bin/node",
                "https://nodejs.org/dist/v16.15.1/node-v16.15.1-darwin-x64.tar.gz",
                "90d0612bbe5467b6cf385c91a68b8daad0057e3e0ccacea44567f5b95b14f7481cb79784185ab1463b4bd990e092ff0f910957"
                "6d1a1934b84e1c816582929611",
                "node-v16.15.1-darwin-x64.tar.gz",
            }};

        int default_tool = -1;
        for (std::size_t i = 0; i < tool_data.size(); i++)
        {
            ArchToolData& d = tool_data[i];
            if (d.tool == toolname && d.arch.has_value() && d.arch.value_or_exit(VCPKG_LINE_INFO) == arch && d.os == os)
            {
                return d;
            }
            else if (d.tool == toolname && d.os == os && !d.arch.has_value())
            {
                default_tool = i;
            }
        }
        if (default_tool >= 0)
        {
            return tool_data[default_tool];
        }

        return nullopt;
    }

    static Optional<ToolData> get_tool_data(StringView tool)
    {
        auto hp = get_host_processor();
#if defined(_WIN32)
        auto data = get_raw_tool_data(tool, hp, "windows");
#elif defined(__APPLE__)
        auto data = get_raw_tool_data(tool, hp, "osx");
#elif defined(__linux__)
        auto data = get_raw_tool_data(tool, hp, "linux");
#elif defined(__FreeBSD__)
        auto data = get_raw_tool_data(tool, hp, "freebsd");
#elif defined(__OpenBSD__)
        auto data = get_raw_tool_data(tool, hp, "openbsd");
#else
        return nullopt;
#endif
        if (!data.has_value())
        {
            return nullopt;
        }
        auto& d = data.value_or_exit(VCPKG_LINE_INFO);
        const Optional<std::array<int, 3>> version = parse_tool_version_string(d.version);
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               version.has_value(),
                               msgFailedToParseVersionXML,
                               msg::tool_name = tool,
                               msg::version = d.version);

        Path tool_dir_name = fmt::format("{}-{}-{}", tool, d.version, d.os);
        Path download_subpath;
        if (!d.archiveName.empty())
        {
            download_subpath = d.archiveName;
        }
        else if (!d.exeRelativePath.empty())
        {
            download_subpath = Strings::concat(d.sha512.substr(0, 8), '-', d.exeRelativePath);
        }

        return ToolData{tool.to_string(),
                        *version.get(),
                        d.exeRelativePath,
                        std::string(d.url),
                        download_subpath,
                        !d.archiveName.empty(),
                        tool_dir_name,
                        std::string(d.sha512)};
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

    struct ToolCacheImpl final : ToolCache
    {
        const Filesystem& fs;
        const std::shared_ptr<const DownloadManager> downloader;
        const Path downloads;
        const Path tools;
        const RequireExactVersions abiToolVersionHandling;

        vcpkg::Cache<std::string, PathAndVersion> path_version_cache;

        ToolCacheImpl(const Filesystem& fs,
                      const std::shared_ptr<const DownloadManager>& downloader,
                      Path downloads,
                      Path tools,
                      RequireExactVersions abiToolVersionHandling)
            : fs(fs)
            , downloader(downloader)
            , downloads(std::move(downloads))
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
            const auto maybe_tool_data = get_tool_data(tool.tool_data_name());
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
                // If there is an entry for the tool in tools.cpp, use that version as the minimum
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
                                              Path tools,
                                              RequireExactVersions abiToolVersionHandling)
    {
        return std::make_unique<ToolCacheImpl>(fs, std::move(downloader), downloads, tools, abiToolVersionHandling);
    }
}
