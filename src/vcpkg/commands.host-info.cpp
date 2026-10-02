#include <vcpkg/base/system-headers.h>

#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/commands.host-info.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/triplet.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

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
        if (!VerQueryValueW(verbuf.data(), L"\\", &rootblock, &rootblocksize)) return nullopt;

        auto rootblock_ffi = static_cast<VS_FIXEDFILEINFO*>(rootblock);
        return fmt::format("{}.{}.{}",
                           static_cast<int>(HIWORD(rootblock_ffi->dwProductVersionMS)),
                           static_cast<int>(LOWORD(rootblock_ffi->dwProductVersionMS)),
                           static_cast<int>(HIWORD(rootblock_ffi->dwProductVersionLS)));
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
        {EnvironmentVariableVcpkgBinarySources, ValueList::None},
        {EnvironmentVariableVcpkgDefaultBinaryCache, ValueList::None},
        {EnvironmentVariableVcpkgUseNuGetCache, ValueList::None},
        {EnvironmentVariableVcpkgNuGetRepository, ValueList::None},
        {EnvironmentVariableVcpkgMaxConcurrency, ValueList::None},
        {EnvironmentVariableVcpkgDisableMetrics, ValueList::None},
        {EnvironmentVariableVcpkgNoCi, ValueList::None},
        {EnvironmentVariableVcpkgForceDownloadedBinaries, ValueList::None},
        {EnvironmentVariableVcpkgForceSystemBinaries, ValueList::None},
        {EnvironmentVariableVcpkgSSLRevokeBestEffort, ValueList::None},
        {EnvironmentVariableVcpkgVisualStudioPath, ValueList::None},
        {EnvironmentVariableXVcpkgAssetSources, ValueList::None},
        {EnvironmentVariableXVcpkgRegistriesCache, ValueList::None},
        {EnvironmentVariableXVcpkgNuGetIDPrefix, ValueList::None},
        {EnvironmentVariableXVcpkgIgnoreLockFailures, ValueList::None},
        // Not vcpkg's own, but they change what it does.
        {EnvironmentVariableAndroidNdkHome, ValueList::None},
        {EnvironmentVariableVCInstallDir, ValueList::None},
        {EnvironmentVariableVsLang, ValueList::None},
        {EnvironmentVariableEditor, ValueList::None},
        {EnvironmentVariableHttpProxy, ValueList::None},
        {EnvironmentVariableHttpsProxy, ValueList::None},
        {EnvironmentVariableNoProxy, ValueList::None},
        {EnvironmentVariableCurlCaBundle, ValueList::None},
        {EnvironmentVariableGitCeilingDirectories, ValueList::None},
    };

    std::string format_environment_value(const EnvironmentVariableEntry& entry, const std::string& value)
    {
        switch (entry.list)
        {
            case ValueList::None: return value;
            case ValueList::Paths: return Strings::join(", ", Strings::split_paths(value));
            case ValueList::Comma: return Strings::join(", ", Strings::split(value, ','));
            case ValueList::Semicolon: return Strings::join(", ", Strings::split(value, ';'));
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
        // get_generic_cmake_build_args() in commands.build.cpp. The platform
        // toolset and git path are left out on purpose, because resolving them
        // selects a Visual Studio instance and can acquire git, which a command
        // that only reports should not do.
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
