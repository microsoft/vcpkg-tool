#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/fmt.h>
#include <vcpkg/base/fwd/optional.h>
#include <vcpkg/base/fwd/stringview.h>
#include <vcpkg/base/fwd/system.h>

#include <string>
#include <vector>

namespace vcpkg
{
    Optional<std::string> get_environment_variable(ZStringView varname) noexcept;
    void set_environment_variable(ZStringView varname, Optional<ZStringView> value) noexcept;

    std::vector<std::string> get_environment_variables();

    const ExpectedL<Path>& get_home_dir() noexcept;

    const ExpectedL<Path>& get_platform_cache_root() noexcept;

    const ExpectedL<Path>& get_platform_cache_vcpkg() noexcept;

    const ExpectedL<Path>& get_user_configuration_home() noexcept;

#ifdef _WIN32
    const ExpectedL<Path>& get_appdata_local() noexcept;

    const ExpectedL<Path>& get_system_drive() noexcept;

    const ExpectedL<Path>& get_system_root() noexcept;

    const ExpectedL<Path>& get_system32() noexcept;

    std::wstring get_username();

    bool test_registry_key(void* base_hkey, StringView sub_key);

    ExpectedL<std::string> get_registry_string(void* base_hkey, StringView subkey, StringView valuename);

    ExpectedL<std::uint32_t> get_registry_dword(void* base_hkey, StringView subkey, StringView valuename);

    void reset_processor_architecture_environment_variable();
#endif

    long get_process_id();

    Optional<CPUArchitecture> to_cpu_architecture(StringView arch) noexcept;

    StringLiteral to_string_literal(CPUArchitecture arch) noexcept;

    LocalizedString all_comma_separated_cpu_architectures();

    CPUArchitecture get_host_processor();

    std::string get_host_os_name();

    std::vector<CPUArchitecture> get_supported_host_architectures();

    const Optional<Path>& get_program_files_32_bit();

    const Optional<Path>& get_program_files_platform_bitness();

    unsigned int get_concurrency();

    Optional<CPUArchitecture> guess_visual_studio_prompt_target_architecture();
}

VCPKG_FORMAT_WITH_TO_STRING_LITERAL_NONMEMBER(vcpkg::CPUArchitecture);
