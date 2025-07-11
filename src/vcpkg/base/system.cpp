#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/uuid.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#if defined(__linux__)
#include <sched.h>
#endif

#if defined(_WIN32)
#include <lmcons.h>
#include <winbase.h>
// needed for mingw
#include <processenv.h>
#else
extern char** environ;
#endif

namespace
{
    using namespace vcpkg;
#ifdef _WIN32
    struct RegistryValue
    {
        DWORD type;
        std::vector<unsigned char> data;
    };

    const HKEY INVALID_HKEY_VALUE = ((HKEY)(ULONG_PTR)((LONG)0xFFFFFFFF));
    struct HKey
    {
        HKEY hkey = INVALID_HKEY_VALUE;

        static ExpectedL<HKey> open(void* base_hkey, StringView sub_key, DWORD desired_access)
        {
            HKEY constructed_hkey = nullptr;
            const LSTATUS ec = RegOpenKeyExW(reinterpret_cast<HKEY>(base_hkey),
                                             Strings::to_utf16(sub_key).c_str(),
                                             0,
                                             desired_access,
                                             &constructed_hkey);
            if (ec == ERROR_SUCCESS)
            {
                return HKey{constructed_hkey};
            }
            else
            {
                return LocalizedString::from_raw(std::system_category().message(static_cast<int>(ec)));
            }
        }

        HKey(HKey&& other) : hkey(std::exchange(other.hkey, INVALID_HKEY_VALUE)) { }
        HKey& operator=(HKey&& other)
        {
            HKey moved{std::move(other)};
            std::swap(hkey, moved.hkey);
            return *this;
        }
        ~HKey()
        {
            if (hkey != INVALID_HKEY_VALUE)
            {
                RegCloseKey(hkey);
            }
        }

        ExpectedL<RegistryValue> query_value(StringView valuename) const
        {
            if (hkey == INVALID_HKEY_VALUE)
            {
                Checks::unreachable(VCPKG_LINE_INFO, "Tried to query invalid key");
            }

            auto w_valuename = Strings::to_utf16(valuename);
            RegistryValue result;
            DWORD dw_buffer_size = 4;
            for (;;)
            {
                result.data.resize(dw_buffer_size);
                LSTATUS attempt_result = ::RegQueryValueExW(
                    hkey, w_valuename.c_str(), nullptr, &result.type, result.data.data(), &dw_buffer_size);
                switch (attempt_result)
                {
                    case ERROR_SUCCESS: result.data.resize(dw_buffer_size); return result;
                    case ERROR_MORE_DATA: continue;
                    default: return LocalizedString::from_raw(std::system_category().message(attempt_result));
                }
            }
        }

        explicit operator bool() const noexcept { return hkey != INVALID_HKEY_VALUE; }

    private:
        explicit HKey(HKEY constructed_hkey) : hkey(constructed_hkey) { }
    };

    StringLiteral format_base_hkey_name(void* base_hkey)
    {
        // copied these values out of winreg.h because HKEY can't be used as a switch/case as it isn't integral
        switch (reinterpret_cast<std::uintptr_t>(base_hkey))
        {
            case 0x80000000u: return "HKEY_CLASSES_ROOT";
            case 0x80000001u: return "HKEY_CURRENT_USER";
            case 0x80000002u: return "HKEY_LOCAL_MACHINE";
            case 0x80000003u: return "HKEY_USERS";
            case 0x80000004u: return "HKEY_PERFORMANCE_DATA";
            case 0x80000005u: return "HKEY_CURRENT_CONFIG";
            case 0x80000050u: return "HKEY_PERFORMANCE_TEXT";
            case 0x80000060u: return "HKEY_PERFORMANCE_NLSTEXT";
            default: return "UNKNOWN_BASE_HKEY";
        }
    }

    std::string format_registry_value_name(void* base_hkey, StringView sub_key, StringView valuename)
    {
        auto result = format_base_hkey_name(base_hkey).to_string();
        if (!sub_key.empty())
        {
            result.push_back('\\');
            result.append(sub_key.data(), sub_key.size());
        }

        result.append(2, '\\');
        result.append(valuename.data(), valuename.size());
        return result;
    }
#endif // ^^^ _WIN32

    struct CPUArchitectureEntry
    {
        StringLiteral name;
        CPUArchitecture arch;
    };

    // keep this in sync with vcpkg-tools.schema.json
    static constexpr CPUArchitectureEntry cpu_architecture_table[] = {
        {"x86", CPUArchitecture::X86},
        {"x64", CPUArchitecture::X64},
        {"amd64", CPUArchitecture::X64},
        {"arm", CPUArchitecture::ARM},
        {"arm64", CPUArchitecture::ARM64},
        {"arm64ec", CPUArchitecture::ARM64EC},
        {"s390x", CPUArchitecture::S390X},
        {"ppc64le", CPUArchitecture::PPC64LE},
        {"riscv32", CPUArchitecture::RISCV32},
        {"riscv64", CPUArchitecture::RISCV64},
        {"loongarch32", CPUArchitecture::LOONGARCH32},
        {"loongarch64", CPUArchitecture::LOONGARCH64},
        {"mips64", CPUArchitecture::MIPS64},
    };
}

namespace vcpkg
{
    long get_process_id()
    {
#ifdef _WIN32
        return ::_getpid();
#else
        return ::getpid();
#endif // ^^^ !_WIN32
    }

    Optional<CPUArchitecture> to_cpu_architecture(StringView arch) noexcept
    {
        for (auto&& entry : cpu_architecture_table)
        {
            if (Strings::case_insensitive_ascii_equals(arch, entry.name))
            {
                return entry.arch;
            }
        }

        return nullopt;
    }

    StringLiteral to_string_literal(CPUArchitecture arch) noexcept
    {
        for (auto&& entry : cpu_architecture_table)
        {
            if (entry.arch == arch)
            {
                return entry.name;
            }
        }

        Checks::unreachable(VCPKG_LINE_INFO, "unexpected vcpkg::CPUArchitecture");
    }

    LocalizedString all_comma_separated_cpu_architectures()
    {
        return LocalizedString::from_raw(
            Strings::join(", ", cpu_architecture_table, [](const CPUArchitectureEntry& entry) { return entry.name; }));
    }

    CPUArchitecture get_host_processor()
    {
#if defined(_WIN32)
        const HMODULE hKernel32 = ::GetModuleHandleW(L"kernel32.dll");
        if (hKernel32)
        {
            BOOL(__stdcall* const isWow64Process2)(
                HANDLE /* hProcess */, USHORT* /* pProcessMachine */, USHORT* /*pNativeMachine*/) =
                reinterpret_cast<decltype(isWow64Process2)>(::GetProcAddress(hKernel32, "IsWow64Process2"));
            if (isWow64Process2)
            {
                USHORT processMachine;
                USHORT nativeMachine;
                if (isWow64Process2(::GetCurrentProcess(), &processMachine, &nativeMachine))
                {
                    Debug::println("Detecting host with IsWow64Process2");
                    switch (nativeMachine)
                    {
                        case 0x014c: // IMAGE_FILE_MACHINE_I386
                            return CPUArchitecture::X86;
                        case 0x01c0: // IMAGE_FILE_MACHINE_ARM
                        case 0x01c2: // IMAGE_FILE_MACHINE_THUMB
                        case 0x01c4: // IMAGE_FILE_MACHINE_ARMNT
                            return CPUArchitecture::ARM;
                        case 0x8664: // IMAGE_FILE_MACHINE_AMD64
                            return CPUArchitecture::X64;
                        case 0xAA64: // IMAGE_FILE_MACHINE_ARM64
                            return CPUArchitecture::ARM64;
                        default: Checks::unreachable(VCPKG_LINE_INFO);
                    }
                }
            }
        }

        Debug::println("Could not use IsWow64Process2, trying IsWow64Process");
        BOOL isWow64Legacy;
        if (::IsWow64Process(::GetCurrentProcess(), &isWow64Legacy))
        {
            if (isWow64Legacy)
            {
                Debug::println("Is WOW64, assuming host is X64");
                return CPUArchitecture::X64;
            }
        }
        else
        {
            Debug::println("IsWow64Process failed, falling back to compiled architecture.");
        }

#if defined(_M_IX86)
        return CPUArchitecture::X86;
#elif defined(_M_ARM)
        return CPUArchitecture::ARM;
#elif defined(_M_ARM64)
        return CPUArchitecture::ARM64;
#elif defined(_M_X64)
        return CPUArchitecture::X64;
#elif defined(__mips64)
        return CPUArchitecture::MIPS64;
#else
#error "Unknown host architecture"
#endif // architecture
#else  // ^^^ defined(_WIN32) / !defined(_WIN32) vvv
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
#elif defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 32)
        return CPUArchitecture::RISCV32;
#elif defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 64)
        return CPUArchitecture::RISCV64;
#elif defined(__loongarch32) || defined(__loongarch__) && (__loongarch_grlen == 32)
        return CPUArchitecture::LOONGARCH32;
#elif defined(__loongarch64) || defined(__loongarch__) && (__loongarch_grlen == 64)
        return CPUArchitecture::LOONGARCH64;
#elif defined(__mips64)
        return CPUArchitecture::MIPS64;
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

    std::vector<std::string> get_environment_variables()
    {
        std::vector<std::string> result;
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
            result.emplace_back(Strings::to_utf8(i, len));
        }
#else
        for (char** s = environ; *s; s++)
        {
            result.emplace_back(*s);
        }
#endif
        return result;
    }

    const ExpectedL<Path>& get_home_dir() noexcept
    {
        static ExpectedL<Path> s_home = []() -> ExpectedL<Path> {
#ifdef _WIN32
            constexpr StringLiteral HOMEVAR = EnvironmentVariableUserprofile;
#else  // ^^^ _WIN32 // !_WIN32 vvv
            constexpr StringLiteral HOMEVAR = EnvironmentVariableHome;
#endif // ^^^ !_WIN32

            auto maybe_home = get_environment_variable(HOMEVAR);
            if (!maybe_home.has_value() || maybe_home.get()->empty())
            {
                return msg::format(msgUnableToReadEnvironmentVariable,
                                   msg::env_var = format_environment_variable(HOMEVAR));
            }

            Path p = std::move(*maybe_home.get());
            if (!p.is_absolute())
            {
                return msg::format(
                    msgEnvVarMustBeAbsolutePath, msg::path = p, msg::env_var = format_environment_variable(HOMEVAR));
            }

            return p;
        }();

        return s_home;
#undef HOMEVAR
    }

#ifdef _WIN32
    const ExpectedL<Path>& get_appdata_local() noexcept
    {
        static ExpectedL<Path> s_home = []() -> ExpectedL<Path> {
            auto maybe_home = get_environment_variable(EnvironmentVariableLocalAppData);
            if (!maybe_home.has_value() || maybe_home.get()->empty())
            {
                // Consult %APPDATA% as a workaround for Service accounts
                // Microsoft/vcpkg#12285
                maybe_home = get_environment_variable(EnvironmentVariableAppData);
                if (!maybe_home.has_value() || maybe_home.get()->empty())
                {
                    return msg::format(msgUnableToReadAppDatas);
                }

                auto p = Path(Path(*maybe_home.get()).parent_path());
                p /= "Local";
                if (!p.is_absolute())
                {
                    return msg::format(msgEnvVarMustBeAbsolutePath,
                                       msg::path = p,
                                       msg::env_var = format_environment_variable(EnvironmentVariableAppData));
                }

                return p;
            }

            auto p = Path(*maybe_home.get());
            if (!p.is_absolute())
            {
                return msg::format(msgEnvVarMustBeAbsolutePath,
                                   msg::path = p,
                                   msg::env_var = format_environment_variable(EnvironmentVariableLocalAppData));
            }

            return p;
        }();
        return s_home;
    }

    static ExpectedL<Path> get_windows_forced_environment_variable(StringLiteral environment_variable)
    {
        auto env = get_environment_variable(environment_variable);
        if (const auto p = env.get())
        {
            return Path(std::move(*p));
        }

        return msg::format(msgWindowsEnvMustAlwaysBePresent,
                           msg::env_var = format_environment_variable(environment_variable));
    }

    const ExpectedL<Path>& get_system_drive() noexcept
    {
        static const ExpectedL<Path> s_system_drive =
            get_windows_forced_environment_variable(EnvironmentVariableSystemDrive);
        return s_system_drive;
    }

    const ExpectedL<Path>& get_system_root() noexcept
    {
        static const ExpectedL<Path> s_system_root =
            get_windows_forced_environment_variable(EnvironmentVariableSystemRoot);
        return s_system_root;
    }

    const ExpectedL<Path>& get_system32() noexcept
    {
        // This needs to be lowercase or msys-ish tools break. See https://github.com/microsoft/vcpkg-tool/pull/418/
        static const ExpectedL<Path> s_system32 = get_system_root().map([](const Path& p) { return p / "system32"; });
        return s_system32;
    }
#else
    static const ExpectedL<Path>& get_xdg_cache_home() noexcept
    {
        static ExpectedL<Path> s_home = []() -> ExpectedL<Path> {
            auto maybe_home = get_environment_variable("XDG_CACHE_HOME");
            if (auto p = maybe_home.get())
            {
                return Path(std::move(*p));
            }

            return get_home_dir().map([](Path home) {
                home /= ".cache";
                return home;
            });
        }();
        return s_home;
    }
#endif

    const ExpectedL<Path>& get_platform_cache_root() noexcept
    {
        static ExpectedL<Path> s_home =
#if defined(_WIN32)
            get_appdata_local()
#else
            get_xdg_cache_home()
#endif
            ;
        return s_home;
    }

    const ExpectedL<Path>& get_platform_cache_vcpkg() noexcept
    {
        static ExpectedL<Path> s_vcpkg = get_platform_cache_root().map([](const Path& p) { return p / "vcpkg"; });
        return s_vcpkg;
    }

    const ExpectedL<Path>& get_user_configuration_home() noexcept
    {
#if defined(_WIN32)
        static const ExpectedL<Path> result =
            get_appdata_local().map([](const Path& appdata_local) { return appdata_local / "vcpkg"; });
#else
        static const ExpectedL<Path> result = Path(get_environment_variable("HOME").value_or("/var")) / ".vcpkg";
#endif
        return result;
    }

#if defined(_WIN32)
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
        return HKey::open(base_hkey, sub_key, KEY_QUERY_VALUE).has_value();
    }

    ExpectedL<std::string> get_registry_string(void* base_hkey, StringView sub_key, StringView valuename)
    {
        auto maybe_k = HKey::open(base_hkey, sub_key, KEY_QUERY_VALUE);
        if (auto k = maybe_k.get())
        {
            auto maybe_value = k->query_value(valuename);
            if (auto value = maybe_value.get())
            {
                switch (value->type)
                {
                    case REG_SZ:
                    case REG_EXPAND_SZ:
                    {
                        auto length_in_wchar_ts = value->data.size() >> 1;
                        auto as_utf8 =
                            Strings::to_utf8(reinterpret_cast<const wchar_t*>(value->data.data()), length_in_wchar_ts);
                        while (!as_utf8.empty() && as_utf8.back() == 0)
                        {
                            as_utf8.pop_back();
                        }
                        return as_utf8;
                    }
                    default:
                        return msg::format_error(msgRegistryValueWrongType,
                                                 msg::path = format_registry_value_name(base_hkey, sub_key, valuename));
                }
            }

            return std::move(maybe_value).error();
        }

        return std::move(maybe_k).error();
    }

    ExpectedL<std::uint32_t> get_registry_dword(void* base_hkey, StringView sub_key, StringView valuename)
    {
        auto maybe_k = HKey::open(base_hkey, sub_key, KEY_QUERY_VALUE);
        if (auto k = maybe_k.get())
        {
            auto maybe_value = k->query_value(valuename);
            if (auto value = maybe_value.get())
            {
                if (value->type == REG_DWORD && value->data.size() >= sizeof(DWORD))
                {
                    DWORD result{};
                    ::memcpy(&result, value->data.data(), sizeof(DWORD));
                    return result;
                }

                return msg::format_error(msgRegistryValueWrongType,
                                         msg::path = format_registry_value_name(base_hkey, sub_key, valuename));
            }

            return std::move(maybe_value).error();
        }

        return std::move(maybe_k).error();
    }

    void reset_processor_architecture_environment_variable()
    {
        // sometimes we get launched with incorrectly set %PROCESSOR_ARCHITECTURE%; this
        // corrects that as we launch a lot of bits like CMake that expect it to be correctly set:
        // https://cmake.org/cmake/help/latest/variable/CMAKE_HOST_SYSTEM_PROCESSOR.html#windows-platforms
        const wchar_t* value;
        const auto proc = get_host_processor();
        switch (proc)
        {
            case CPUArchitecture::X86: value = L"X86"; break;
            case CPUArchitecture::X64: value = L"AMD64"; break;
            case CPUArchitecture::ARM: value = L"ARM"; break;
            case CPUArchitecture::ARM64: value = L"ARM64"; break;
            default:
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgUnexpectedWindowsArchitecture, msg::actual = proc);
                break;
        }

        Checks::check_exit(VCPKG_LINE_INFO, SetEnvironmentVariableW(L"PROCESSOR_ARCHITECTURE", value) != 0);
    }
#endif

    static const Optional<Path>& get_program_files()
    {
        static const auto PROGRAMFILES = []() -> Optional<Path> {
            auto value = get_environment_variable(EnvironmentVariableProgramFiles);
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
            auto value = get_environment_variable(EnvironmentVariableProgramFilesX86);
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
            auto value = get_environment_variable(EnvironmentVariableProgramW6432);
            if (auto v = value.get())
            {
                return *v;
            }
            return get_program_files();
        }();
        return ProgramW6432;
    }

    unsigned int get_concurrency()
    {
        static unsigned int concurrency = [] {
            auto user_defined_concurrency = get_environment_variable(EnvironmentVariableVcpkgMaxConcurrency);
            if (user_defined_concurrency)
            {
                int res = -1;
                try
                {
                    res = std::stoi(user_defined_concurrency.value_or_exit(VCPKG_LINE_INFO));
                }
                catch (std::exception&)
                {
                    Checks::msg_exit_with_message(
                        VCPKG_LINE_INFO, msgOptionMustBeInteger, msg::option = EnvironmentVariableVcpkgMaxConcurrency);
                }

                if (!(res > 0))
                {
                    Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                                  msgEnvInvalidMaxConcurrency,
                                                  msg::env_var = EnvironmentVariableVcpkgMaxConcurrency,
                                                  msg::value = res);
                }
                return static_cast<unsigned int>(res);
            }
            else
            {
#if defined(__linux__)
                // Get the number of threads we are allowed to run on,
                // this might be less than the number of hardware threads.
                cpu_set_t set;
                if (sched_getaffinity(getpid(), sizeof(set), &set) == 0)
                {
                    return static_cast<unsigned int>(CPU_COUNT(&set)) + 1;
                }
#endif
                return std::thread::hardware_concurrency() + 1;
            }
        }();

        return concurrency;
    }

    Optional<CPUArchitecture> guess_visual_studio_prompt_target_architecture()
    {
        // Check for the "vsdevcmd" infrastructure used by Visual Studio 2017 and later
        const auto vscmd_arg_tgt_arch_env = get_environment_variable(EnvironmentVariableVscmdArgTgtArch);
        if (vscmd_arg_tgt_arch_env)
        {
            return to_cpu_architecture(vscmd_arg_tgt_arch_env.value_or_exit(VCPKG_LINE_INFO));
        }

        // Check for the "vcvarsall" infrastructure used by Visual Studio 2015
        if (get_environment_variable(EnvironmentVariableVCInstallDir))
        {
            const auto Platform = get_environment_variable(EnvironmentVariablePlatform);
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
#elif defined(__SVR4) && defined(__sun)
        return "solaris";
#elif defined(__ANDROID__)
        return "android";
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
