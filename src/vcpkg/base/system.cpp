#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <ctime>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#if defined(_WIN32)
#include <lmcons.h>
#include <winbase.h>
// needed for mingw
#include <processenv.h>
#else
extern char** environ;
#endif

namespace vcpkg
{
    long get_process_id()
    {
#ifdef _WIN32
        return ::_getpid();
#else
        return ::getpid();
#endif
    }

    Optional<CPUArchitecture> to_cpu_architecture(StringView arch)
    {
        if (Strings::case_insensitive_ascii_equals(arch, "x86")) return CPUArchitecture::X86;
        if (Strings::case_insensitive_ascii_equals(arch, "x64")) return CPUArchitecture::X64;
        if (Strings::case_insensitive_ascii_equals(arch, "amd64")) return CPUArchitecture::X64;
        if (Strings::case_insensitive_ascii_equals(arch, "arm")) return CPUArchitecture::ARM;
        if (Strings::case_insensitive_ascii_equals(arch, "arm64")) return CPUArchitecture::ARM64;
        if (Strings::case_insensitive_ascii_equals(arch, "arm64ec")) return CPUArchitecture::ARM64EC;
        if (Strings::case_insensitive_ascii_equals(arch, "s390x")) return CPUArchitecture::S390X;
        if (Strings::case_insensitive_ascii_equals(arch, "ppc64le")) return CPUArchitecture::PPC64LE;
        return nullopt;
    }

    ZStringView to_zstring_view(CPUArchitecture arch) noexcept
    {
        switch (arch)
        {
            case CPUArchitecture::X86: return "x86";
            case CPUArchitecture::X64: return "x64";
            case CPUArchitecture::ARM: return "arm";
            case CPUArchitecture::ARM64: return "arm64";
            case CPUArchitecture::ARM64EC: return "arm64ec";
            case CPUArchitecture::S390X: return "s390x";
            case CPUArchitecture::PPC64LE: return "ppc64le";
            default: Checks::exit_with_message(VCPKG_LINE_INFO, "unexpected vcpkg::CPUArchitecture");
        }
    }

    CPUArchitecture get_host_processor()
    {
#if defined(_WIN32)
        auto raw_identifier = get_environment_variable("PROCESSOR_IDENTIFIER");
        if (const auto id = raw_identifier.get())
        {
            // might be either ARMv8 (64-bit) or ARMv9 (64-bit)
            if (Strings::contains(*id, "ARMv") && Strings::contains(*id, "(64-bit)"))
            {
                return CPUArchitecture::ARM64;
            }
        }

        auto raw_w6432 = get_environment_variable("PROCESSOR_ARCHITEW6432");
        if (const auto w6432 = raw_w6432.get())
        {
            const auto parsed_w6432 = to_cpu_architecture(*w6432);
            if (const auto parsed = parsed_w6432.get())
            {
                return *parsed;
            }

            msg::print(Color::warning, msgProcessorArchitectureW6432Malformed, msg::arch = *w6432);
        }

        const auto raw_processor_architecture = get_environment_variable("PROCESSOR_ARCHITECTURE");
        const auto processor_architecture = raw_processor_architecture.get();
        if (!processor_architecture)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgProcessorArchitectureMissing);
        }

        const auto raw_parsed_processor_architecture = to_cpu_architecture(*processor_architecture);
        if (const auto parsed_processor_architecture = raw_parsed_processor_architecture.get())
        {
            return *parsed_processor_architecture;
        }

        Checks::msg_exit_with_message(
            VCPKG_LINE_INFO, msgProcessorArchitectureMalformed, msg::arch = *processor_architecture);
#else // ^^^ defined(_WIN32) / !defined(_WIN32) vvv
#if defined(__x86_64__) || defined(_M_X64)
#if defined(__APPLE__)
        // check for rosetta 2 emulation
        // see docs:
        // https://developer.apple.com/documentation/apple_silicon/about_the_rosetta_translation_environment#3616845
        int is_translated = 0;
        size_t size = sizeof is_translated;
        if (sysctlbyname("sysctl.proc_translated", &is_translated, &size, nullptr, 0) == -1)
        {
            return CPUArchitecture::X64;
        }
        if (is_translated == 1)
        {
            return CPUArchitecture::ARM64;
        }
#endif // ^^^ macos
        return CPUArchitecture::X64;
#elif defined(__x86__) || defined(_M_X86) || defined(__i386__)
        return CPUArchitecture::X86;
#elif defined(__arm__) || defined(_M_ARM)
        return CPUArchitecture::ARM;
#elif defined(__aarch64__) || defined(_M_ARM64)
        return CPUArchitecture::ARM64;
#elif defined(__s390x__)
        return CPUArchitecture::S390X;
#elif (defined(__ppc64__) || defined(__PPC64__) || defined(__ppc64le__) || defined(__PPC64LE__)) &&                    \
    defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        return CPUArchitecture::PPC64LE;
#else // choose architecture
#error "Unknown host architecture"
#endif // choose architecture
#endif // defined(_WIN32)
    }

    std::vector<CPUArchitecture> get_supported_host_architectures()
    {
        std::vector<CPUArchitecture> supported_architectures;
        supported_architectures.push_back(get_host_processor());

        // AMD64 machines support running x86 applications and ARM64 machines support running ARM applications
        if (supported_architectures.back() == CPUArchitecture::X64)
        {
            supported_architectures.push_back(CPUArchitecture::X86);
        }
        else if (supported_architectures.back() == CPUArchitecture::ARM64)
        {
            supported_architectures.push_back(CPUArchitecture::ARM);
        }

#if defined(_WIN32)
        // On ARM32/64 Windows we can rely on x86 emulation
        if (supported_architectures.front() == CPUArchitecture::ARM ||
            supported_architectures.front() == CPUArchitecture::ARM64)
        {
            supported_architectures.push_back(CPUArchitecture::X86);
        }
#endif // defined(_WIN32)

        return supported_architectures;
    }

    Optional<std::string> get_environment_variable(ZStringView varname) noexcept
    {
#if defined(_WIN32)
        const auto w_varname = Strings::to_utf16(varname);
        const auto sz = GetEnvironmentVariableW(w_varname.c_str(), nullptr, 0);
        if (sz == 0) return nullopt;

        std::wstring ret(sz, L'\0');

        Checks::check_exit(VCPKG_LINE_INFO, MAXDWORD >= ret.size());
        const auto sz2 = GetEnvironmentVariableW(w_varname.c_str(), ret.data(), static_cast<DWORD>(ret.size()));
        Checks::check_exit(VCPKG_LINE_INFO, sz2 + 1 == sz);
        ret.pop_back();
        return Strings::to_utf8(ret.c_str());
#else
        auto v = getenv(varname.c_str());
        if (!v) return nullopt;
        return std::string(v);
#endif
    }

    void set_environment_variable(ZStringView varname, Optional<ZStringView> value) noexcept
    {
#if defined(_WIN32)
        const auto w_varname = Strings::to_utf16(varname);
        const auto w_varcstr = w_varname.c_str();
        BOOL exit_code;
        if (auto v = value.get())
        {
            exit_code = SetEnvironmentVariableW(w_varcstr, Strings::to_utf16(*v).c_str());
        }
        else
        {
            exit_code = SetEnvironmentVariableW(w_varcstr, nullptr);
        }

        Checks::check_exit(VCPKG_LINE_INFO, exit_code != 0);
#else
        if (auto v = value.get())
        {
            Checks::check_exit(VCPKG_LINE_INFO, setenv(varname.c_str(), v->c_str(), 1) == 0);
        }
        else
        {
            Checks::check_exit(VCPKG_LINE_INFO, unsetenv(varname.c_str()) == 0);
        }
#endif
    }

    std::string get_environment_variables()
    {
        std::string result;
#if defined(_WIN32)
        const struct EnvironmentStringsW
        {
            LPWCH strings;
            EnvironmentStringsW() : strings(GetEnvironmentStringsW()) { Checks::check_exit(VCPKG_LINE_INFO, strings); }
            ~EnvironmentStringsW() { FreeEnvironmentStringsW(strings); }
        } env_block;

        size_t len;
        for (LPWCH i = env_block.strings; *i; i += len + 1)
        {
            len = wcslen(i);
            result.append(Strings::to_utf8(i, len)).push_back('\n');
        }
#else
        for (char** s = environ; *s; s++)
        {
            result.append(*s).push_back('\n');
        }
#endif
        return result;
    }

    const ExpectedS<Path>& get_home_dir() noexcept
    {
        static ExpectedS<Path> s_home = []() -> ExpectedS<Path> {
#ifdef _WIN32
#define HOMEVAR "%USERPROFILE%"
            auto maybe_home = get_environment_variable("USERPROFILE");
            if (!maybe_home.has_value() || maybe_home.get()->empty())
                return {"unable to read " HOMEVAR, ExpectedRightTag{}};
#else
#define HOMEVAR "$HOME"
            auto maybe_home = get_environment_variable("HOME");
            if (!maybe_home.has_value() || maybe_home.get()->empty())
                return {"unable to read " HOMEVAR, ExpectedRightTag{}};
#endif

            Path p = *maybe_home.get();
            if (!p.is_absolute()) return {HOMEVAR " was not an absolute path", ExpectedRightTag{}};

            return {std::move(p), ExpectedLeftTag{}};
        }();
        return s_home;
#undef HOMEVAR
    }

#ifdef _WIN32
    const ExpectedS<Path>& get_appdata_local() noexcept
    {
        static ExpectedS<Path> s_home = []() -> ExpectedS<Path> {
            auto maybe_home = get_environment_variable("LOCALAPPDATA");
            if (!maybe_home.has_value() || maybe_home.get()->empty())
            {
                // Consult %APPDATA% as a workaround for Service accounts
                // Microsoft/vcpkg#12285
                maybe_home = get_environment_variable("APPDATA");
                if (!maybe_home.has_value() || maybe_home.get()->empty())
                {
                    return {"unable to read %LOCALAPPDATA% or %APPDATA%", ExpectedRightTag{}};
                }

                auto p = Path(Path(*maybe_home.get()).parent_path());
                p /= "Local";
                if (!p.is_absolute()) return {"%APPDATA% was not an absolute path", ExpectedRightTag{}};
                return {std::move(p), ExpectedLeftTag{}};
            }

            auto p = Path(*maybe_home.get());
            if (!p.is_absolute()) return {"%LOCALAPPDATA% was not an absolute path", ExpectedRightTag{}};

            return {std::move(p), ExpectedLeftTag{}};
        }();
        return s_home;
    }

    const ExpectedS<Path>& get_system_root() noexcept
    {
        static const ExpectedS<Path> s_system_root = []() -> ExpectedS<Path> {
            auto env = get_environment_variable("SystemRoot");
            if (const auto p = env.get())
            {
                return Path(std::move(*p));
            }
            else
            {
                return std::string("Expected the SystemRoot environment variable to be always set on Windows.");
            }
        }();
        return s_system_root;
    }

    const ExpectedS<Path>& get_system32() noexcept
    {
        // This needs to be lowercase or msys-ish tools break. See https://github.com/microsoft/vcpkg-tool/pull/418/
        static const ExpectedS<Path> s_system32 = get_system_root().map([](const Path& p) { return p / "system32"; });
        return s_system32;
    }
#else
    static const ExpectedS<Path>& get_xdg_cache_home() noexcept
    {
        static ExpectedS<Path> s_home = [] {
            auto maybe_home = get_environment_variable("XDG_CACHE_HOME");
            if (auto p = maybe_home.get())
            {
                return ExpectedS<Path>(Path(*p));
            }
            else
            {
                return get_home_dir().map([](Path home) {
                    home /= ".cache";
                    return home;
                });
            }
        }();
        return s_home;
    }
#endif

    const ExpectedS<Path>& get_platform_cache_home() noexcept
    {
#ifdef _WIN32
        return get_appdata_local();
#else
        return get_xdg_cache_home();
#endif
    }

#if defined(_WIN32)
    static bool is_string_keytype(const DWORD hkey_type)
    {
        return hkey_type == REG_SZ || hkey_type == REG_MULTI_SZ || hkey_type == REG_EXPAND_SZ;
    }

    std::wstring get_username()
    {
        DWORD buffer_size = UNLEN + 1;
        std::wstring buffer;
        buffer.resize(static_cast<size_t>(buffer_size));
        GetUserNameW(buffer.data(), &buffer_size);
        buffer.resize(buffer_size);
        return buffer;
    }

    bool test_registry_key(void* base_hkey, StringView sub_key)
    {
        HKEY k = nullptr;
        const LSTATUS ec =
            RegOpenKeyExW(reinterpret_cast<HKEY>(base_hkey), Strings::to_utf16(sub_key).c_str(), 0, KEY_READ, &k);
        return (ERROR_SUCCESS == ec);
    }

    Optional<std::string> get_registry_string(void* base_hkey, StringView sub_key, StringView valuename)
    {
        HKEY k = nullptr;
        const LSTATUS ec =
            RegOpenKeyExW(reinterpret_cast<HKEY>(base_hkey), Strings::to_utf16(sub_key).c_str(), 0, KEY_READ, &k);
        if (ec != ERROR_SUCCESS) return nullopt;

        auto w_valuename = Strings::to_utf16(valuename);

        DWORD dw_buffer_size = 0;
        DWORD dw_type = 0;
        auto rc = RegQueryValueExW(k, w_valuename.c_str(), nullptr, &dw_type, nullptr, &dw_buffer_size);
        if (rc != ERROR_SUCCESS || !is_string_keytype(dw_type) || dw_buffer_size == 0 ||
            dw_buffer_size % sizeof(wchar_t) != 0)
            return nullopt;
        std::wstring ret;
        ret.resize(dw_buffer_size / sizeof(wchar_t));

        rc = RegQueryValueExW(
            k, w_valuename.c_str(), nullptr, &dw_type, reinterpret_cast<LPBYTE>(ret.data()), &dw_buffer_size);
        if (rc != ERROR_SUCCESS || !is_string_keytype(dw_type) || dw_buffer_size != sizeof(wchar_t) * ret.size())
            return nullopt;

        ret.pop_back(); // remove extra trailing null byte
        return Strings::to_utf8(ret);
    }
#else
    Optional<std::string> get_registry_string(void*, StringView, StringView) { return nullopt; }
#endif

    static const Optional<Path>& get_program_files()
    {
        static const auto PROGRAMFILES = []() -> Optional<Path> {
            auto value = get_environment_variable("PROGRAMFILES");
            if (auto v = value.get())
            {
                return *v;
            }

            return nullopt;
        }();

        return PROGRAMFILES;
    }

    const Optional<Path>& get_program_files_32_bit()
    {
        static const auto PROGRAMFILES_x86 = []() -> Optional<Path> {
            auto value = get_environment_variable("ProgramFiles(x86)");
            if (auto v = value.get())
            {
                return *v;
            }
            return get_program_files();
        }();
        return PROGRAMFILES_x86;
    }

    const Optional<Path>& get_program_files_platform_bitness()
    {
        static const auto ProgramW6432 = []() -> Optional<Path> {
            auto value = get_environment_variable("ProgramW6432");
            if (auto v = value.get())
            {
                return *v;
            }
            return get_program_files();
        }();
        return ProgramW6432;
    }

    int get_concurrency()
    {
        static int concurrency = [] {
            auto user_defined_concurrency = get_environment_variable("VCPKG_MAX_CONCURRENCY");
            if (user_defined_concurrency)
            {
                return std::stoi(user_defined_concurrency.value_or_exit(VCPKG_LINE_INFO));
            }
            else
            {
                return static_cast<int>(std::thread::hardware_concurrency()) + 1;
            }
        }();

        return concurrency;
    }

    Optional<CPUArchitecture> guess_visual_studio_prompt_target_architecture()
    {
        // Check for the "vsdevcmd" infrastructure used by Visual Studio 2017 and later
        const auto vscmd_arg_tgt_arch_env = get_environment_variable("VSCMD_ARG_TGT_ARCH");
        if (vscmd_arg_tgt_arch_env)
        {
            return to_cpu_architecture(vscmd_arg_tgt_arch_env.value_or_exit(VCPKG_LINE_INFO));
        }

        // Check for the "vcvarsall" infrastructure used by Visual Studio 2015
        if (get_environment_variable("VCINSTALLDIR"))
        {
            const auto Platform = get_environment_variable("Platform");
            if (Platform)
            {
                return to_cpu_architecture(Platform.value_or_exit(VCPKG_LINE_INFO));
            }
            else
            {
                return CPUArchitecture::X86;
            }
        }

        return nullopt;
    }

    std::string get_host_os_name()
    {
#if defined(_WIN32)
        return "windows";
#elif defined(__APPLE__)
        return "osx";
#elif defined(__FreeBSD__)
        return "freebsd";
#elif defined(__OpenBSD__)
        return "openbsd";
#elif defined(__linux__)
        return "linux";
#else
        return "unknown"
#endif
    }
}

namespace vcpkg::Debug
{
    std::atomic<bool> g_debugging(false);
}
