#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <ctime>

#if defined(__APPLE__)
#include <sys/sysctl.h>
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
            case CPUArchitecture::S390X: return "s390x";
            case CPUArchitecture::PPC64LE: return "ppc64le";
            default: Checks::exit_with_message(VCPKG_LINE_INFO, "unexpected vcpkg::CPUArchitecture");
        }
    }

    CPUArchitecture get_host_processor()
    {
#if defined(_WIN32)
        auto w6432 = get_environment_variable("PROCESSOR_ARCHITEW6432");
        if (const auto p = w6432.get()) return to_cpu_architecture(*p).value_or_exit(VCPKG_LINE_INFO);

        const auto procarch = get_environment_variable("PROCESSOR_ARCHITECTURE").value_or_exit(VCPKG_LINE_INFO);
        return to_cpu_architecture(procarch).value_or_exit(VCPKG_LINE_INFO);
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
#else // ^^^ defined(_WIN32) / !defined(_WIN32) vvv
        auto v = getenv(varname.c_str());
        if (!v) return nullopt;
        return std::string(v);
#endif // defined(_WIN32)
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
#else // ^^^ defined(_WIN32) / !defined(_WIN32) vvv
        if (auto v = value.get())
        {
            Checks::check_exit(VCPKG_LINE_INFO, setenv(varname.c_str(), v->c_str(), 1) == 0);
        }
        else
        {
            Checks::check_exit(VCPKG_LINE_INFO, unsetenv(varname.c_str()) == 0);
        }
#endif // defined(_WIN32)
    }

    const ExpectedS<path>& get_home_dir() noexcept
    {
        static ExpectedS<path> s_home = []() -> ExpectedS<path> {
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

            auto p = vcpkg::u8path(*maybe_home.get());
            if (!p.is_absolute()) return {HOMEVAR " was not an absolute path", ExpectedRightTag{}};

            return {std::move(p), ExpectedLeftTag{}};
        }();
        return s_home;
#undef HOMEVAR
    }

#ifdef _WIN32
    const ExpectedS<path>& get_appdata_local() noexcept
    {
        static ExpectedS<path> s_home = []() -> ExpectedS<path> {
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

                auto p = vcpkg::u8path(*maybe_home.get()).parent_path();
                p /= "Local";
                if (!p.is_absolute()) return {"%APPDATA% was not an absolute path", ExpectedRightTag{}};
                return {std::move(p), ExpectedLeftTag{}};
            }

            auto p = vcpkg::u8path(*maybe_home.get());
            if (!p.is_absolute()) return {"%LOCALAPPDATA% was not an absolute path", ExpectedRightTag{}};

            return {std::move(p), ExpectedLeftTag{}};
        }();
        return s_home;
    }
#else
    static const ExpectedS<path>& get_xdg_cache_home() noexcept
    {
        static ExpectedS<path> s_home = [] {
            auto maybe_home = get_environment_variable("XDG_CACHE_HOME");
            if (auto p = maybe_home.get())
            {
                return ExpectedS<path>(vcpkg::u8path(*p));
            }
            else
            {
                return get_home_dir().map([](path home) {
                    home /= vcpkg::u8path(".cache");
                    return home;
                });
            }
        }();
        return s_home;
    }
#endif

    const ExpectedS<path>& get_platform_cache_home() noexcept
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
#else // ^^^ defined(_WIN32) / !defined(_WIN32) vvv
    Optional<std::string> get_registry_string(void*, StringView, StringView) { return nullopt; }
#endif // defined(_WIN32)

    static const Optional<path>& get_program_files()
    {
        static const auto PROGRAMFILES = []() -> Optional<path> {
            auto value = get_environment_variable("PROGRAMFILES");
            if (auto v = value.get())
            {
                return *v;
            }

            return nullopt;
        }();

        return PROGRAMFILES;
    }

    const Optional<path>& get_program_files_32_bit()
    {
        static const auto PROGRAMFILES_x86 = []() -> Optional<path> {
            auto value = get_environment_variable("ProgramFiles(x86)");
            if (auto v = value.get())
            {
                return *v;
            }
            return get_program_files();
        }();
        return PROGRAMFILES_x86;
    }

    const Optional<path>& get_program_files_platform_bitness()
    {
        static const auto ProgramW6432 = []() -> Optional<path> {
            auto value = get_environment_variable("ProgramW6432");
            if (auto v = value.get())
            {
                return *v;
            }
            return get_program_files();
        }();
        return ProgramW6432;
    }

    int get_num_logical_cores() { return std::thread::hardware_concurrency(); }

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
}

namespace vcpkg::Debug
{
    std::atomic<bool> g_debugging(false);
}
