#include <vcpkg/base/system-headers.h>

#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.host-info.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/triplet.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/visualstudio.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

#if defined(_WIN32)
#pragma comment(lib, "version")
#else
#include <sys/utsname.h>
#endif

#if defined(__GLIBC__)
#include <gnu/libc-version.h>
#endif

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

using namespace vcpkg;

namespace
{
    // One entry per line, so a value that carries newlines is flattened rather
    // than breaking the format.
    std::string one_line(StringView value)
    {
        std::string result;
        bool pending_space = false;
        for (char ch : value)
        {
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            {
                pending_space = true;
                continue;
            }

            if (pending_space && !result.empty())
            {
                result.push_back(' ');
            }

            pending_space = false;
            result.push_back(ch);
        }

        return result;
    }

    // The same shape as the `depend-info` list format: a name, a colon, and a
    // comma separated list of values.
    void print_entry(StringView name, StringView value)
    {
        msg::write_unlocalized_text(Color::success, name);
        msg::write_unlocalized_text(Color::none, fmt::format(": {}\n", one_line(value)));
    }

    // Not every platform's os info is made of optional values, so this goes
    // unused on some of them.
    [[maybe_unused]] void print_entry_if_set(StringView name, const Optional<std::string>& value)
    {
        if (auto v = value.get())
        {
            print_entry(name, *v);
        }
    }

#if defined(_WIN32)
    // The major.minor.build of the product version in a file's version resource.
    Optional<std::string> get_file_product_version(const std::wstring& path)
    {
        const auto versz = GetFileVersionInfoSizeW(path.c_str(), nullptr);
        if (versz == 0) return nullopt;

        std::vector<char> verbuf;
        verbuf.resize(versz);
        if (!GetFileVersionInfoW(path.c_str(), 0, static_cast<DWORD>(verbuf.size()), verbuf.data()))
        {
            return nullopt;
        }

        void* rootblock;
        UINT rootblocksize;
        if (VerQueryValueW(verbuf.data(), L"\\", &rootblock, &rootblocksize))
        {
            auto rootblock_ffi = static_cast<VS_FIXEDFILEINFO*>(rootblock);
            if (rootblock_ffi->dwProductVersionMS != 0 || rootblock_ffi->dwProductVersionLS != 0)
            {
                return fmt::format("{}.{}.{}",
                                   static_cast<int>(HIWORD(rootblock_ffi->dwProductVersionMS)),
                                   static_cast<int>(LOWORD(rootblock_ffi->dwProductVersionMS)),
                                   static_cast<int>(HIWORD(rootblock_ffi->dwProductVersionLS)));
            }
        }

        // Some files, LLVM's among them, leave the fixed version zero and give
        // it only as a string, under the first language they list.
        struct LanguageAndCodePage
        {
            WORD language;
            WORD code_page;
        };

        void* translations;
        UINT translations_size;
        if (!VerQueryValueW(verbuf.data(), L"\\VarFileInfo\\Translation", &translations, &translations_size) ||
            translations_size < sizeof(LanguageAndCodePage))
        {
            return nullopt;
        }

        const auto translation = static_cast<const LanguageAndCodePage*>(translations);
        const auto query = Strings::to_utf16(fmt::format(
            "\\StringFileInfo\\{:04x}{:04x}\\ProductVersion", translation->language, translation->code_page));
        void* product_version;
        UINT product_version_size;
        if (!VerQueryValueW(verbuf.data(), query.c_str(), &product_version, &product_version_size) ||
            product_version_size == 0)
        {
            return nullopt;
        }

        // The size counts the terminating null.
        return Strings::to_utf8(static_cast<const wchar_t*>(product_version), product_version_size - 1);
    }

    // The version resource of kernel32.dll rather than GetVersionEx, which lies
    // about anything past the manifested compatibility of this process.
    Optional<std::string> get_windows_kernel_version()
    {
        std::wstring path;
        path.resize(MAX_PATH);
        const auto n = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
        if (n == 0) return nullopt;
        path.resize(n);
        path += L"\\kernel32.dll";
        return get_file_product_version(path);
    }

    constexpr StringLiteral CurrentVersionKey = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

    Optional<std::string> registry_string(StringView value_name)
    {
        auto maybe_value = get_registry_string(HKEY_LOCAL_MACHINE, CurrentVersionKey, value_name);
        if (auto value = maybe_value.get())
        {
            return *value;
        }

        return nullopt;
    }

    void print_os_info()
    {
        print_entry_if_set("os-version", get_windows_kernel_version());
        auto maybe_build = registry_string("CurrentBuild");

        // "Client", "Server" or "Server Core". Server editions name themselves
        // correctly in ProductName, so this also says whether the rename below
        // applies.
        auto maybe_installation_type = registry_string("InstallationType");
        const bool is_client =
            !maybe_installation_type.has_value() || maybe_installation_type.value_or_exit(VCPKG_LINE_INFO) == "Client";

        // ProductName was never updated for Windows 11 and still reads
        // "Windows 10 ..." there, so go by the build number instead, as
        // Microsoft's own tooling does. 22000 is the first Windows 11 build.
        // Windows Server is exempt: it is versioned by release year, and a
        // Server build number crossing 22000 must not be renamed.
        auto maybe_edition = registry_string("ProductName");
        if (auto edition = maybe_edition.get())
        {
            auto build_number = maybe_build.get();
            if (is_client && build_number && Strings::starts_with(*edition, "Windows 10"))
            {
                auto maybe_parsed = Strings::strto<int>(*build_number);
                if (auto parsed = maybe_parsed.get())
                {
                    if (*parsed >= 22000)
                    {
                        edition->replace(0, StringLiteral("Windows 10").size(), "Windows 11");
                    }
                }
            }

            print_entry("os-edition", *edition);
        }

        print_entry_if_set("os-edition-id", registry_string("EditionID"));
        print_entry_if_set("os-installation-type", maybe_installation_type);
        print_entry_if_set("os-release", registry_string("DisplayVersion"));

        // The update build revision is a DWORD, and is what distinguishes one
        // cumulative update from the next within a build.
        if (auto build = maybe_build.get())
        {
            auto maybe_ubr = get_registry_dword(HKEY_LOCAL_MACHINE, CurrentVersionKey, "UBR");
            if (auto ubr = maybe_ubr.get())
            {
                print_entry("os-build", fmt::format("{}.{}", *build, *ubr));
            }
            else
            {
                print_entry("os-build", *build);
            }
        }
    }

    // The version, the platform toolset, which targets the toolset can compile
    // for, and whether it has ATL and MFC. The last two are per toolset: each
    // MSVC version is installed with its own set of target architectures and
    // its own ATL and MFC, so one toolset of an instance having them says
    // nothing about the next.
    std::string describe_toolset(const Path& vs_root, const Toolset& toolset)
    {
        std::vector<std::string> parts{toolset.full_version, toolset.version.to_string()};
        const auto msvc_dir = vs_root / "VC\\Tools\\MSVC" / toolset.full_version;
        if (real_filesystem.is_directory(msvc_dir))
        {
            // The compilers this host runs natively, which is where vcvarsall
            // takes them from.
            const auto host = to_string_literal(get_host_processor());
            std::vector<std::string> targets;
            for (auto&& dir : real_filesystem.get_directories_non_recursive(
                     msvc_dir / "bin" / ("Host" + host.to_string()), IgnoreErrors{}))
            {
                if (real_filesystem.exists(dir / "cl.exe", IgnoreErrors{}))
                {
                    targets.push_back(dir.filename().to_string());
                }
            }

            if (targets.empty())
            {
                parts.push_back(fmt::format("no {} host compiler", host));
            }
            else
            {
                parts.push_back(fmt::format("{} host targets {}", host, Strings::join(" ", targets)));
            }

            if (real_filesystem.exists(msvc_dir / "atlmfc\\include\\atlbase.h", IgnoreErrors{}))
            {
                parts.push_back("atlmfc");
            }
        }
        else
        {
            // Visual Studio 2015 and earlier, which have no per version
            // directory, and the v140 toolset a later instance runs from an
            // installed Visual Studio 2015. Only the vcvarsall.bat scripts say
            // what these target.
            parts.push_back("vcvars " +
                            Strings::join(" ",
                                          Util::fmap(toolset.supported_architectures,
                                                     [](const ToolsetArchOption& arch) { return arch.name; })));
        }

        return Strings::join(", ", parts);
    }

    // Each instance on its own line, followed by what vcpkg would find in it,
    // so the lines that follow a visual-studio line belong to that instance.
    void print_visual_studio_info()
    {
        for (auto&& instance : VisualStudio::get_visual_studio_instance_details(real_filesystem))
        {
            std::vector<std::string> description{instance.root_path.native()};
            if (!instance.display_name.empty()) description.push_back(instance.display_name);
            if (!instance.display_version.empty()) description.push_back(instance.display_version);
            description.push_back(instance.version);
            description.push_back(instance.release_type);
            print_entry("visual-studio", Strings::join(", ", description));

            std::vector<StringLiteral> problems;
            if (!instance.is_complete) problems.push_back("incomplete");
            if (instance.is_reboot_required) problems.push_back("reboot required");
            if (!problems.empty())
            {
                print_entry("visual-studio-problems", Strings::join(", ", problems));
            }

            for (auto&& toolset : instance.toolsets)
            {
                print_entry("visual-studio-msvc-toolset", describe_toolset(instance.root_path, toolset));
            }

            // What vcvarsall selects when no -vcvars_ver is given, as it would
            // be outside vcpkg.
            std::error_code ec;
            auto default_toolset = real_filesystem.read_contents(
                instance.root_path / "VC\\Auxiliary\\Build\\Microsoft.VCToolsVersion.default.txt", ec);
            if (!ec)
            {
                auto trimmed = Strings::trim(StringView{default_toolset});
                if (!trimmed.empty())
                {
                    print_entry("visual-studio-msvc-default", trimmed);
                }
            }

            // The clang-cl installed with the instance, by host architecture,
            // and where Visual Studio 2019 put it for an x86 host.
            static constexpr StringLiteral clang_bin_dirs[] = {
                "VC\\Tools\\Llvm\\x64\\bin",
                "VC\\Tools\\Llvm\\ARM64\\bin",
                "VC\\Tools\\Llvm\\bin",
            };

            for (auto&& bin_dir : clang_bin_dirs)
            {
                auto clang_cl = instance.root_path / bin_dir / "clang-cl.exe";
                if (!real_filesystem.exists(clang_cl, IgnoreErrors{})) continue;

                auto maybe_version = get_file_product_version(Strings::to_utf16(clang_cl.native()));
                if (auto version = maybe_version.get())
                {
                    print_entry("visual-studio-clang-cl", fmt::format("{}, {}", *version, clang_cl.native()));
                }
                else
                {
                    print_entry("visual-studio-clang-cl", clang_cl.native());
                }

                break;
            }
        }
    }

    // The kits root as winsdk.bat finds it when vcvarsall runs it, looking in
    // the same places in the same order.
    Optional<std::string> get_windows_kits_root(StringView value_name)
    {
        static constexpr StringLiteral subkeys[] = {
            "SOFTWARE\\Wow6432Node\\Microsoft\\Windows Kits\\Installed Roots",
            "SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
        };

        for (auto&& subkey : subkeys)
        {
            for (auto base : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
            {
                auto maybe_root = get_registry_string(base, subkey, value_name);
                if (auto root = maybe_root.get())
                {
                    if (!root->empty()) return std::move(*root);
                }
            }
        }

        return nullopt;
    }

    // The architectures an SDK can link for, by the libraries vcvarsall needs
    // for each: kernel32.lib for the SDK itself and, from the Windows 10 SDK
    // on, ucrt.lib for the C runtime, which winsdk.bat also checks for.
    std::string describe_sdk_architectures(const Path& lib_dir, bool has_ucrt)
    {
        std::vector<std::string> architectures;
        for (auto&& dir : real_filesystem.get_directories_non_recursive(lib_dir / "um", IgnoreErrors{}))
        {
            const auto arch = dir.filename();
            if (!real_filesystem.exists(dir / "kernel32.lib", IgnoreErrors{})) continue;
            if (has_ucrt && !real_filesystem.exists(lib_dir / "ucrt" / arch / "ucrt.lib", IgnoreErrors{}))
            {
                continue;
            }

            architectures.push_back(arch.to_string());
        }

        if (architectures.empty()) return "headers only";
        return Strings::join(" ", architectures);
    }

    void print_windows_sdk_info()
    {
        // A version counts only if it has the header winsdk.bat checks for
        // when building for the desktop; an uninstalled SDK can leave its
        // directory behind.
        auto maybe_root10 = get_windows_kits_root("KitsRoot10");
        if (auto root10 = maybe_root10.get())
        {
            print_entry("windows-kits-root", *root10);

            std::vector<std::string> versions;
            for (auto&& dir : real_filesystem.get_directories_non_recursive(Path(*root10) / "Include", IgnoreErrors{}))
            {
                if (Strings::starts_with(dir.filename(), "10.") &&
                    real_filesystem.exists(dir / "um\\winsdkver.h", IgnoreErrors{}))
                {
                    versions.push_back(dir.filename().to_string());
                }
            }

            // Every Windows 10 and later SDK has a five digit build number, so
            // this is also numeric order, latest first.
            std::sort(versions.begin(), versions.end(), std::greater<>{});
            for (auto&& version : versions)
            {
                print_entry(
                    "windows-sdk",
                    fmt::format("{}, {}", version, describe_sdk_architectures(Path(*root10) / "Lib" / version, true)));
            }
        }

        auto maybe_root81 = get_windows_kits_root("KitsRoot81");
        if (auto root81 = maybe_root81.get())
        {
            if (real_filesystem.exists(Path(*root81) / "Include\\um\\winsdkver.h", IgnoreErrors{}))
            {
                // Windows 8.1 is Windows 6.3, and its C runtime came with
                // Visual Studio rather than with the SDK.
                print_entry("windows-sdk",
                            fmt::format("8.1, {}", describe_sdk_architectures(Path(*root81) / "Lib\\winv6.3", false)));
            }
        }
    }

    // Without this, paths are limited to MAX_PATH, 260 characters, in vcpkg
    // and in every build tool it runs, and deep build trees run past that.
    // vcpkg declares itself long path aware, so this setting alone decides.
    void print_long_paths()
    {
        auto maybe_enabled = get_registry_dword(
            HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\FileSystem", "LongPathsEnabled");
        const auto enabled = maybe_enabled.get();
        print_entry("os-long-paths", enabled && *enabled != 0 ? "enabled" : "disabled");
    }
#else // ^^^ defined(_WIN32) // !defined(_WIN32) vvv
    Optional<utsname> get_utsname()
    {
        utsname buf;
        if (uname(&buf) != 0) return nullopt;
        return buf;
    }

#if defined(__APPLE__)
    Optional<std::string> get_sysctl_string(const char* name)
    {
        size_t size = 0;
        if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) return nullopt;
        std::string result(size, '\0');
        if (sysctlbyname(name, result.data(), &size, nullptr, 0) != 0) return nullopt;
        while (!result.empty() && result.back() == '\0')
        {
            result.pop_back();
        }

        return result;
    }
#endif // ^^^ defined(__APPLE__)

#if defined(__linux__) && !defined(__GLIBC__)
    // musl deliberately defines no version macro and offers nothing like
    // glibc's gnu_get_libc_version(), but its dynamic loader reports itself
    // when run with no arguments:
    //
    //     musl libc (x86_64)
    //     Version 1.2.5
    //
    // so ask that. It writes this to stderr and exits non-zero doing it, so
    // neither the exit code nor the stream is inspected, and the capture used
    // below has to be one that merges stderr in.
    Optional<std::string> get_musl_description()
    {
        std::error_code ec;
        auto candidates = real_filesystem.get_regular_files_non_recursive("/lib", ec);
        if (ec) return nullopt;

        for (auto&& candidate : candidates)
        {
            if (!Strings::starts_with(candidate.filename(), "ld-musl-")) continue;

            auto maybe_output = cmd_execute_and_capture_output(Command{candidate.native()});
            if (auto output = maybe_output.get())
            {
                static constexpr StringLiteral VersionPrefix = "Version ";
                for (auto&& line : Strings::split(output->output, '\n'))
                {
                    auto trimmed = Strings::trim(StringView{line});
                    if (Strings::starts_with(trimmed, VersionPrefix))
                    {
                        auto version = trimmed.substr(VersionPrefix.size());
                        return "musl " + std::string(version.data(), version.size());
                    }
                }
            }

            // The loader is there, so this is musl even if it would not say
            // which version it is.
            return std::string("musl");
        }

        return nullopt;
    }
#endif // ^^^ defined(__linux__) && !defined(__GLIBC__)

#if defined(__linux__)
    std::string get_libc_description()
    {
#if defined(__GLIBC__)
        return fmt::format("glibc {}", gnu_get_libc_version());
#else
        auto maybe_musl = get_musl_description();
        if (auto musl = maybe_musl.get())
        {
            return *musl;
        }

        return "unknown, not glibc";
#endif
    }
#endif // ^^^ defined(__linux__)

    void print_os_info()
    {
#if defined(__APPLE__)
        print_entry_if_set("os-version", get_sysctl_string("kern.osproductversion"));
        print_entry_if_set("os-build", get_sysctl_string("kern.osversion"));
        auto maybe_uts = get_utsname();
        if (auto uts = maybe_uts.get())
        {
            print_entry("kernel-version", uts->release);
        }
#elif defined(__linux__)
        auto maybe_uts = get_utsname();
        if (auto uts = maybe_uts.get())
        {
            print_entry("kernel-version", uts->release);
            print_entry("kernel-build", uts->version);
        }

        print_entry("libc", get_libc_description());
#endif // the BSDs and anything else get os-name alone
    }
#endif // ^^^ !defined(_WIN32)

    // The output, with stderr merged in, of a run that succeeds.
    Optional<std::string> run_tool(const Path& program,
                                   std::initializer_list<StringView> args,
                                   std::string stdin_content = {})
    {
        Command cmd{program.native()};
        for (auto&& arg : args)
        {
            cmd.string_arg(arg);
        }

        RedirectedProcessLaunchSettings settings;
        settings.stdin_content = std::move(stdin_content);
        auto maybe_output = cmd_execute_and_capture_output(cmd, settings);
        if (auto output = maybe_output.get())
        {
            if (output->exit_code == 0) return std::move(output->output);
        }

        return nullopt;
    }

    StringView first_line(StringView text)
    {
        auto end = std::find(text.begin(), text.end(), '\n');
        return Strings::trim(StringView{text.data(), static_cast<size_t>(end - text.begin())});
    }

#if defined(__APPLE__)
    // The string value of a key in an XML property list, such as the
    // version.plist every Xcode has.
    Optional<std::string> get_plist_string(const std::string& plist, StringView key)
    {
        const auto key_element = fmt::format("<key>{}</key>", key);
        auto key_position = plist.find(key_element);
        if (key_position == std::string::npos) return nullopt;

        static constexpr StringLiteral Open = "<string>";
        static constexpr StringLiteral Close = "</string>";
        const auto after_key = key_position + key_element.size();
        auto open = plist.find(Open.data(), after_key, Open.size());
        if (open == std::string::npos) return nullopt;

        // The value has to be the element right after the key.
        if (!Strings::trim(StringView{plist}.substr(after_key, open - after_key)).empty()) return nullopt;

        const auto value_start = open + Open.size();
        auto close = plist.find(Close.data(), value_start, Close.size());
        if (close == std::string::npos) return nullopt;
        return plist.substr(value_start, close - value_start);
    }

    // The Xcode an application bundle is, as its version and build, read
    // from the bundle rather than from xcodebuild, which would first have
    // its license accepted.
    Optional<std::string> describe_xcode(const Path& app)
    {
        std::error_code ec;
        auto plist = real_filesystem.read_contents(app / "Contents/version.plist", ec);
        if (ec) return nullopt;

        auto maybe_version = get_plist_string(plist, "CFBundleShortVersionString");
        auto version = maybe_version.get();
        if (!version) return nullopt;

        auto maybe_build = get_plist_string(plist, "ProductBuildVersion");
        if (auto build = maybe_build.get())
        {
            return fmt::format("{}, {}", *version, *build);
        }

        return *version;
    }

    void print_xcode_info()
    {
        // Which developer directory xcrun, and the compilers in /usr/bin, use.
        // Asking xcode-select never prompts to install anything, but running
        // xcrun on a Mac without developer tools pops up an installer, so the
        // rest is asked only once this says there is a developer directory.
        Optional<Path> developer_dir;
        auto maybe_selected = run_tool("/usr/bin/xcode-select", {"-p"});
        if (auto selected = maybe_selected.get())
        {
            Path dir(first_line(*selected));
            if (!dir.native().empty() && real_filesystem.is_directory(dir))
            {
                print_entry("xcode-developer-dir", dir.native());
                developer_dir = std::move(dir);
            }
        }

        if (!developer_dir)
        {
            print_entry("xcode-developer-dir", "none");
        }

        // Every Xcode installed where the App Store and Apple's downloads put
        // it, as there can be several, such as a beta beside a release, and
        // the selected one wherever it is.
        std::vector<Path> apps;
        for (auto&& app : real_filesystem.get_directories_non_recursive("/Applications", IgnoreErrors{}))
        {
            auto name = app.filename();
            if (Strings::starts_with(name, "Xcode") && Strings::ends_with(name, ".app"))
            {
                apps.push_back(app);
            }
        }

        static constexpr StringLiteral AppDeveloperDir = ".app/Contents/Developer";
        Optional<Path> selected_app;
        if (auto dir = developer_dir.get())
        {
            if (Strings::ends_with(dir->native(), AppDeveloperDir))
            {
                selected_app = Path(dir->parent_path()).parent_path();
                if (!Util::contains(apps, *selected_app.get()))
                {
                    apps.push_back(*selected_app.get());
                }
            }
        }

        for (auto&& app : apps)
        {
            auto maybe_description = describe_xcode(app);
            if (auto description = maybe_description.get())
            {
                const bool selected = selected_app.has_value() && *selected_app.get() == app;
                print_entry("xcode", fmt::format("{}, {}{}", app.native(), *description, selected ? ", selected" : ""));
            }
        }

        // The Command Line Tools are installed apart from Xcode, and used when
        // they are what is selected or when there is no Xcode at all.
        auto maybe_clt = run_tool("/usr/sbin/pkgutil", {"--pkg-info=com.apple.pkg.CLTools_Executables"});
        if (auto clt = maybe_clt.get())
        {
            static constexpr StringLiteral VersionPrefix = "version: ";
            for (auto&& line : Strings::split(*clt, '\n'))
            {
                auto trimmed = Strings::trim(StringView{line});
                if (Strings::starts_with(trimmed, VersionPrefix))
                {
                    print_entry("xcode-command-line-tools", trimmed.substr(VersionPrefix.size()));
                    break;
                }
            }
        }

        if (!developer_dir) return;

        // What a build gets by default from the selected developer directory:
        // the macOS SDK, unless SDKROOT names another, and the compiler.
        auto maybe_sdk_version = run_tool("/usr/bin/xcrun", {"--sdk", "macosx", "--show-sdk-version"});
        auto maybe_sdk_path = run_tool("/usr/bin/xcrun", {"--sdk", "macosx", "--show-sdk-path"});
        if (auto sdk_version = maybe_sdk_version.get())
        {
            if (auto sdk_path = maybe_sdk_path.get())
            {
                print_entry("macos-sdk", fmt::format("{}, {}", first_line(*sdk_version), first_line(*sdk_path)));
            }
            else
            {
                print_entry("macos-sdk", first_line(*sdk_version));
            }
        }

        auto maybe_clang = run_tool("/usr/bin/xcrun", {"clang", "--version"});
        if (auto clang = maybe_clang.get())
        {
            print_entry("xcode-clang", first_line(*clang));
        }
    }
#endif // ^^^ defined(__APPLE__)

    // Everything about a MinGW toolchain is asked of its compiler rather than
    // looked up in a package database: MinGW comes from MSYS2, from standalone
    // builds such as MinGW-Builds, WinLibs and llvm-mingw, and from Linux and
    // macOS cross compiler packages, and only the compiler knows what it does
    // in all of them.

#if defined(_WIN32)
    constexpr StringLiteral NullDevice = "NUL";
#else
    constexpr StringLiteral NullDevice = "/dev/null";
#endif

    // The macros the preprocessor defines with these headers included, as
    // printed by -dM.
    std::map<std::string, std::string, std::less<>> get_compiler_defines(const Path& compiler,
                                                                         StringLiteral language,
                                                                         StringLiteral header)
    {
        std::map<std::string, std::string, std::less<>> defines;
        auto maybe_output =
            run_tool(compiler, {"-x", language, "-E", "-dM", "-"}, fmt::format("#include <{}>\n", header));
        if (auto output = maybe_output.get())
        {
            static constexpr StringLiteral Define = "#define ";
            for (auto&& line : Strings::split(*output, '\n'))
            {
                auto trimmed = Strings::trim(StringView{line});
                if (!Strings::starts_with(trimmed, Define)) continue;
                trimmed = trimmed.substr(Define.size());
                auto space = std::find(trimmed.begin(), trimmed.end(), ' ');
                std::string name(trimmed.begin(), space);
                std::string value = space == trimmed.end() ? std::string() : std::string(space + 1, trimmed.end());
                defines.emplace(std::move(name), std::move(value));
            }
        }

        return defines;
    }

    Optional<std::string> get_define(const std::map<std::string, std::string, std::less<>>& defines, StringView name)
    {
        auto it = defines.find(name);
        if (it == defines.end()) return nullopt;
        return it->second;
    }

    // A file the compiler would link, or nothing if it would not find it:
    // -print-file-name prints the name back unchanged then.
    Optional<Path> find_compiler_file(const Path& compiler, StringLiteral file)
    {
        auto maybe_output = run_tool(compiler, {"-print-file-name=" + file.to_string()});
        if (auto output = maybe_output.get())
        {
            auto found = first_line(*output);
            if (found != file)
            {
                auto path = Path(found).lexically_normal();
                path.make_preferred();
                return path;
            }
        }

        return nullopt;
    }

    // The compiler that built a static library, from the identification
    // string each compiler leaves in the objects it writes.
    Optional<std::string> get_library_builder(const Path& library)
    {
        std::error_code ec;
        auto contents = real_filesystem.read_contents(library, ec);
        if (ec) return nullopt;

        for (StringLiteral marker : {StringLiteral{"GCC: ("}, StringLiteral{"clang version "}})
        {
            auto start = contents.find(marker.data(), 0, marker.size());
            if (start == std::string::npos) continue;
            auto end = contents.find_first_of(std::string("\0\n", 2), start);
            if (end == std::string::npos) end = contents.size();
            return contents.substr(start, end - start);
        }

        return nullopt;
    }

    // libc++ has given its version as MMmmpp since 16, and as Mmmpp, or
    // MMmpp, before that.
    std::string describe_libcxx_version(StringView value)
    {
        auto maybe_version = Strings::strto<long>(value);
        if (auto version = maybe_version.get())
        {
            const long major_divisor = *version >= 160000 ? 10000 : 1000;
            const long major = *version / major_divisor;
            const long rest = *version % major_divisor;
            return fmt::format("libc++ {}.{}.{}", major, rest / 100, rest % 100);
        }

        return "libc++ " + value.to_string();
    }

    // The compiler is printed on its own line, and the lines that follow it,
    // up to the next mingw-compiler line, describe it.
    void print_mingw_compiler(const Path& compiler, StringView target)
    {
        print_entry("mingw-compiler", fmt::format("{}, {}", compiler.native(), target));

        auto maybe_version = run_tool(compiler, {"--version"});
        if (auto version = maybe_version.get())
        {
            print_entry("mingw-compiler-version", first_line(*version));
        }

        // posix, win32 or mcf: which threads std::thread is built on, and so
        // whether programs need winpthreads, or mcfgthread, at run time.
        auto maybe_verbose = run_tool(compiler, {"-v"});
        if (auto verbose = maybe_verbose.get())
        {
            static constexpr StringLiteral ThreadModel = "Thread model: ";
            for (auto&& line : Strings::split(*verbose, '\n'))
            {
                auto trimmed = Strings::trim(StringView{line});
                if (Strings::starts_with(trimmed, ThreadModel))
                {
                    print_entry("mingw-thread-model", trimmed.substr(ThreadModel.size()));
                    break;
                }
            }
        }

        // _mingw.h is where mingw-w64 states its own version and which C
        // runtime the headers are configured for.
        const auto c_defines = get_compiler_defines(compiler, "c", "_mingw.h");
        auto maybe_major = get_define(c_defines, "__MINGW64_VERSION_MAJOR");
        if (auto major = maybe_major.get())
        {
            auto headers = fmt::format("{}.{}.{}",
                                       *major,
                                       get_define(c_defines, "__MINGW64_VERSION_MINOR").value_or("0"),
                                       get_define(c_defines, "__MINGW64_VERSION_BUGFIX").value_or("0"));
            auto maybe_state = get_define(c_defines, "__MINGW64_VERSION_STATE");
            if (auto state = maybe_state.get())
            {
                headers += " " + Strings::replace_all(*state, "\"", "");
            }

            print_entry("mingw-headers", headers);
        }
        else if (!c_defines.empty())
        {
            // The original mingw.org project, which some old toolchains
            // still ship.
            print_entry("mingw-headers", "not mingw-w64");
        }

        auto maybe_msvcrt_version = get_define(c_defines, "__MSVCRT_VERSION__");
        if (c_defines.count("_UCRT"))
        {
            print_entry("mingw-crt", "ucrt");
        }
        else if (auto msvcrt_version = maybe_msvcrt_version.get())
        {
            print_entry("mingw-crt", "msvcrt " + *msvcrt_version);
        }

        // Predefined whether or not anything is included, so only meaningful
        // once the preprocessor ran at all.
        if (!c_defines.empty())
        {
            StringLiteral exceptions = "dwarf";
            if (c_defines.count("__SEH__"))
            {
                exceptions = "seh";
            }
            else if (c_defines.count("__USING_SJLJ_EXCEPTIONS__"))
            {
                exceptions = "sjlj";
            }

            print_entry("mingw-exceptions", exceptions);
            print_entry_if_set("mingw-default-win32-winnt", get_define(c_defines, "_WIN32_WINNT"));
        }

        // Any header of the C++ library defines its version macros.
        const auto cxx_defines = get_compiler_defines(compiler, "c++", "cstddef");
        auto maybe_libcxx = get_define(cxx_defines, "_LIBCPP_VERSION");
        auto maybe_glibcxx = get_define(cxx_defines, "__GLIBCXX__");
        if (auto libcxx = maybe_libcxx.get())
        {
            print_entry("mingw-cxx-library", describe_libcxx_version(*libcxx));
        }
        else if (auto glibcxx = maybe_glibcxx.get())
        {
            auto maybe_release = get_define(cxx_defines, "_GLIBCXX_RELEASE");
            if (auto release = maybe_release.get())
            {
                print_entry("mingw-cxx-library", fmt::format("libstdc++ {}, {}", *release, *glibcxx));
            }
            else
            {
                print_entry("mingw-cxx-library", "libstdc++ " + *glibcxx);
            }
        }

        // libgcc, or compiler-rt for clang configured to use it.
        auto maybe_runtime = run_tool(compiler, {"-print-libgcc-file-name"});
        if (auto runtime = maybe_runtime.get())
        {
            auto runtime_file = first_line(*runtime);
            if (!runtime_file.empty())
            {
                print_entry("mingw-runtime-library", runtime_file.contains("clang_rt") ? "compiler-rt" : "libgcc");
            }
        }

        // The linker the compiler runs by default, which is not necessarily
        // the one named ld on the PATH. Both GNU ld and LLD exit as soon as
        // they have printed their version, so nothing is linked or written.
        auto maybe_linker =
            run_tool(compiler, {"-x", "c", "-", "-o", NullDevice, "-Wl,--version"}, "int main(void) { return 0; }\n");
        if (auto linker = maybe_linker.get())
        {
            for (auto&& line : Strings::split(*linker, '\n'))
            {
                auto trimmed = Strings::trim(StringView{line});
                if (Strings::starts_with(trimmed, "GNU ld") || Strings::starts_with(trimmed, "LLD") ||
                    Strings::starts_with(trimmed, "GNU gold"))
                {
                    print_entry("mingw-linker", trimmed);
                    break;
                }
            }
        }

        // The import libraries and the rest of the mingw-w64 CRT carry no
        // version of their own: they come from the same mingw-w64 release as
        // the headers. Where they are, and which compiler built them, is what
        // tells a toolchain mixed from different installs apart.
        auto maybe_kernel32 = find_compiler_file(compiler, "libkernel32.a");
        if (auto kernel32 = maybe_kernel32.get())
        {
            print_entry("mingw-import-libraries", kernel32->parent_path());
        }

        auto maybe_mingwex = find_compiler_file(compiler, "libmingwex.a");
        if (auto mingwex = maybe_mingwex.get())
        {
            print_entry_if_set("mingw-crt-built-by", get_library_builder(*mingwex));
        }
    }

    // gcc and clang as they would be run by name, and the target prefixed gcc
    // that vcpkg's MinGW toolchain looks for. Every one on the PATH is listed,
    // and each name in PATH order, so one shadowing another of the same name
    // shows. Compilers that do not target MinGW, such as clang for MSVC, are
    // left out.
    void print_mingw_compilers()
    {
        static constexpr StringLiteral compiler_names[] = {
            "gcc",
            "clang",
            "x86_64-w64-mingw32-gcc",
            "i686-w64-mingw32-gcc",
            "aarch64-w64-mingw32-gcc",
            "armv7-w64-mingw32-gcc",
        };

        // A toolchain usually has both gcc and its target prefixed name in
        // one directory; that is one compiler, reported once.
        std::vector<std::string> seen;
        for (auto&& name : compiler_names)
        {
            for (auto&& compiler : real_filesystem.find_from_PATH(name))
            {
                auto maybe_target = run_tool(compiler, {"-dumpmachine"});
                auto target_output = maybe_target.get();
                if (!target_output) continue;

                auto target = first_line(*target_output);
                if (!target.contains("-mingw32") && !target.contains("-windows-gnu")) continue;

                auto key = fmt::format("{}|{}|{}",
                                       compiler.parent_path(),
                                       target,
                                       Strings::case_insensitive_ascii_contains(compiler.filename(), "clang"));
                if (Util::contains(seen, key)) continue;
                seen.push_back(std::move(key));

                print_mingw_compiler(compiler, target);
            }
        }
    }

    // How vcpkg itself splits a variable that holds more than one value, so the
    // list printed here is the list vcpkg acts on.
    enum class ValueList
    {
        // A single value, printed as it is.
        None,
        // The platform path separator, as Strings::split_paths uses.
        Paths,
        Comma,
        Semicolon,
        // A single URL, with any credentials in it replaced.
        Url,
        // Binary or asset sources, with anything that can be a credential
        // replaced: this is pasted into public bug reports.
        Sources,
    };

    struct EnvironmentVariableEntry
    {
        StringLiteral name;
        ValueList list;
    };

    constexpr EnvironmentVariableEntry environment_variables[] = {
        {EnvironmentVariableVcpkgRoot, ValueList::None},
        {EnvironmentVariableVcpkgCommand, ValueList::None},
        {EnvironmentVariableVcpkgDefaultTriplet, ValueList::None},
        {EnvironmentVariableVcpkgDefaultHostTriplet, ValueList::None},
        {EnvironmentVariableVcpkgOverlayPorts, ValueList::Paths},
        {EnvironmentVariableOverlayTriplets, ValueList::Paths},
        {EnvironmentVariableVcpkgFeatureFlags, ValueList::Comma},
        {EnvironmentVariableVcpkgKeepEnvVars, ValueList::Semicolon},
        {EnvironmentVariableVcpkgDownloads, ValueList::None},
        {EnvironmentVariableVcpkgBinarySources, ValueList::Sources},
        {EnvironmentVariableVcpkgDefaultBinaryCache, ValueList::None},
        {EnvironmentVariableVcpkgUseNuGetCache, ValueList::None},
        {EnvironmentVariableVcpkgNuGetRepository, ValueList::Url},
        {EnvironmentVariableVcpkgMaxConcurrency, ValueList::None},
        {EnvironmentVariableVcpkgDisableMetrics, ValueList::None},
        {EnvironmentVariableVcpkgNoCi, ValueList::None},
        {EnvironmentVariableVcpkgForceDownloadedBinaries, ValueList::None},
        {EnvironmentVariableVcpkgForceSystemBinaries, ValueList::None},
        {EnvironmentVariableVcpkgSSLRevokeBestEffort, ValueList::None},
        {EnvironmentVariableVcpkgVisualStudioPath, ValueList::None},
        {EnvironmentVariableXVcpkgAssetSources, ValueList::Sources},
        {EnvironmentVariableXVcpkgRegistriesCache, ValueList::None},
        {EnvironmentVariableXVcpkgNuGetIDPrefix, ValueList::None},
        {EnvironmentVariableXVcpkgIgnoreLockFailures, ValueList::None},
        // Not vcpkg's own, but they change what it does.
        {EnvironmentVariableAndroidNdkHome, ValueList::None},
        {EnvironmentVariableVCInstallDir, ValueList::None},
        {EnvironmentVariableVsLang, ValueList::None},
        // Set by a Visual Studio developer prompt, and say which one this is
        // run from, if any. A stray Platform breaks MSBuild based ports.
        {EnvironmentVariableVscmdVer, ValueList::None},
        {EnvironmentVariableVscmdArgHostArch, ValueList::None},
        {EnvironmentVariableVscmdArgTgtArch, ValueList::None},
        {EnvironmentVariableVCToolsVersion, ValueList::None},
        {EnvironmentVariableWindowsSdkVersion, ValueList::None},
        {EnvironmentVariablePlatform, ValueList::None},
        // Which Xcode, SDK and minimum macOS version a build uses, instead of
        // the selected ones.
        {EnvironmentVariableDeveloperDir, ValueList::None},
        {EnvironmentVariableSdkRoot, ValueList::None},
        {EnvironmentVariableMacosxDeploymentTarget, ValueList::None},
        {EnvironmentVariableEditor, ValueList::None},
        {EnvironmentVariableHttpProxy, ValueList::Url},
        {EnvironmentVariableHttpsProxy, ValueList::Url},
        {EnvironmentVariableNoProxy, ValueList::None},
        {EnvironmentVariableCurlCaBundle, ValueList::None},
        {EnvironmentVariableGitCeilingDirectories, ValueList::None},
    };

    constexpr StringLiteral Redacted = "***";

    // A URL with the credentials that can be written into one replaced: a
    // user and password before the host, and the query, which is where
    // tokens such as Azure SAS go.
    std::string redact_url(StringView url)
    {
        std::string result = url.to_string();
        const auto scheme_end = result.find("://");
        if (scheme_end == std::string::npos) return result;

        const auto authority_start = scheme_end + 3;
        auto authority_end = result.find_first_of("/?#", authority_start);
        if (authority_end == std::string::npos) authority_end = result.size();
        const auto at = result.rfind('@', authority_end);
        if (at != std::string::npos && at >= authority_start && at < authority_end)
        {
            result.replace(authority_start, at - authority_start, Redacted.data(), Redacted.size());
        }

        const auto query = result.find('?', authority_start);
        if (query != std::string::npos)
        {
            result.replace(query + 1, std::string::npos, Redacted.data(), Redacted.size());
        }

        return result;
    }

    // The pieces of text separated by separator, as written, except where a
    // backtick escapes the separator, as in binary and asset sources.
    std::vector<StringView> split_unescaped(StringView text, char separator)
    {
        std::vector<StringView> pieces;
        size_t start = 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '`')
            {
                ++i;
            }
            else if (text[i] == separator)
            {
                pieces.push_back(text.substr(start, i - start));
                start = i + 1;
            }
        }

        pieces.push_back(text.substr(start));
        return pieces;
    }

    // The sources as written, but with only what cannot be a credential left
    // readable: each source's kind, its first argument with any credentials
    // in a URL replaced, and the read and write modes. Every other argument,
    // SAS tokens and HTTP headers among them, is replaced. The parsers' own
    // list of secrets is not used for this: it does not cover every
    // argument that can hold one, and is lost when a source does not parse.
    std::string redact_sources(StringView value)
    {
        std::vector<std::string> sources;
        for (auto source : split_unescaped(value, ';'))
        {
            auto args = split_unescaped(source, ',');
            std::vector<std::string> kept;
            for (size_t i = 0; i < args.size(); ++i)
            {
                const auto arg = args[i];
                if (i == 0 || arg.empty() || arg == "read" || arg == "write" || arg == "readwrite")
                {
                    kept.push_back(arg.to_string());
                }
                else if (i == 1)
                {
                    kept.push_back(redact_url(arg));
                }
                else
                {
                    kept.push_back(Redacted.to_string());
                }
            }

            sources.push_back(Strings::join(",", kept));
        }

        return Strings::join(";", sources);
    }

    std::string format_environment_value(const EnvironmentVariableEntry& entry, const std::string& value)
    {
        switch (entry.list)
        {
            case ValueList::None: return value;
            case ValueList::Paths: return Strings::join(", ", Strings::split_paths(value));
            case ValueList::Comma: return Strings::join(", ", Strings::split(value, ','));
            case ValueList::Semicolon: return Strings::join(", ", Strings::split(value, ';'));
            case ValueList::Url: return redact_url(value);
            case ValueList::Sources: return redact_sources(value);
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandHostInfoMetadata{
        "host-info",
        msgHelpHostInfoCommand,
        {"vcpkg host-info"},
        "https://learn.microsoft.com/vcpkg/commands/host-info",
        AutocompletePriority::Public,
        0,
        0,
        {},
        nullptr,
    };

    void command_host_info_and_exit(const VcpkgCmdArguments& args,
                                    const VcpkgPaths& paths,
                                    Triplet default_triplet,
                                    Triplet host_triplet)
    {
        (void)args.parse_arguments(CommandHostInfoMetadata);

        print_entry("os-name", get_host_os_name());
        print_os_info();
#if defined(_WIN32)
        print_long_paths();
        print_visual_studio_info();
        print_windows_sdk_info();
#endif
#if defined(__APPLE__)
        print_xcode_info();
#endif
        print_mingw_compilers();

        print_entry("vcpkg-executable", get_exe_path_of_current_process().native());
        print_entry("vcpkg-root", paths.root.native());
        print_entry("host-triplet", host_triplet.canonical_name());

        // What the CMake toolchain in the registry takes from this tool. Only
        // values this tool resolves itself are listed: the toolchain file lives
        // in the registry rather than here, so its own variables are not known.
        // VCPKG_HOST_TRIPLET is deliberately absent: the toolchain only reads
        // it, never sets it, so it has a value here only when the user supplied
        // one. The host triplet in effect is reported above instead.
        print_entry("$TOOLCHAIN{VCPKG_TARGET_TRIPLET}", default_triplet.canonical_name());
        if (auto installed = paths.maybe_installed().get())
        {
            print_entry("$TOOLCHAIN{VCPKG_INSTALLED_DIR}", installed->root().native());
            print_entry("$TOOLCHAIN{VCPKG_MANIFEST_MODE}", paths.manifest_mode_enabled() ? "ON" : "OFF");
        }

        // The CMake variables passed into every port build, less the ones that
        // only exist once a particular port is being built: see
        // get_generic_cmake_build_args() in commands.build.cpp. CMD is left
        // out too: it is always BUILD, so it says nothing about the host. The
        // platform toolset and git path are left out on purpose, because
        // resolving them selects a Visual Studio instance and can acquire git,
        // which a command that only reports should not do.
        print_entry("$PORT{DOWNLOADS}", paths.downloads.native());
        print_entry("$PORT{TARGET_TRIPLET}", default_triplet.canonical_name());
        print_entry("$PORT{TARGET_TRIPLET_FILE}",
                    paths.get_triplet_db().get_triplet_file_path(default_triplet).native());
        print_entry("$PORT{_HOST_TRIPLET}", host_triplet.canonical_name());
        print_entry("$PORT{VCPKG_BASE_VERSION}", VCPKG_BASE_VERSION_AS_STRING);
        print_entry("$PORT{VCPKG_CONCURRENCY}", std::to_string(get_concurrency()));

        for (auto&& entry : environment_variables)
        {
            auto maybe_value = get_environment_variable(entry.name);
            if (auto value = maybe_value.get())
            {
                print_entry("$ENV{" + std::string(entry.name.data(), entry.name.size()) + "}",
                            format_environment_value(entry, *value));
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
