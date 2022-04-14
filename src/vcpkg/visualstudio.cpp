#if defined(_WIN32)

#include <vcpkg/base/files.h>
#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/visualstudio.h>

#endif

#include <vcpkg/base/messages.h>

namespace
{
    DECLARE_AND_REGISTER_MESSAGE(VSExaminedPaths,
                                 (),
                                 "",
                                 "The following paths were examined for Visual Studio instances:");

    DECLARE_AND_REGISTER_MESSAGE(VSNoInstances, (), "", "Could not locate a complete Visual Studio instance");

    DECLARE_AND_REGISTER_MESSAGE(VSExaminedInstances, (), "", "The following Visual Studio instances were considered:");

    DECLARE_AND_REGISTER_MESSAGE(
        VSNoNativeDesktop,
        (),
        "",
        "Could not find Desktop development with C++, please install it using Visual Studio Installer.");

    DECLARE_AND_REGISTER_MESSAGE(VSNoCoreFeatures,
                                 (),
                                 "",
                                 "Could not find c++ core features, please install it using Visual Studio Installer.");

    DECLARE_AND_REGISTER_MESSAGE(VSNoMSBuild,
                                 (),
                                 "",
                                 "Could not find msbuild, please install it using Visual Studio Installer.");

    DECLARE_AND_REGISTER_MESSAGE(
        VSNoUCRT,
        (),
        "",
        "Could not find Windows Universal C Runtime, please install it using Visual Studio Installer.");

    DECLARE_AND_REGISTER_MESSAGE(VSNoWindowsSDK,
                                 (),
                                 "",
                                 "Could not find Windows SDK, please install it using Visual Studio Installer.");

    DECLARE_AND_REGISTER_MESSAGE(
        VSNoARMVCTools,
        (),
        "",
        "Could not find Visual Studio Build tools for ARM, please install it using Visual Studio Installer.");

    DECLARE_AND_REGISTER_MESSAGE(
        VSNoARM64VCTools,
        (),
        "",
        "Could not find Visual Studio Build tools for ARM64, please install it using Visual Studio Installer.");

    DECLARE_AND_REGISTER_MESSAGE(
        VSNoUWPVCTools,
        (),
        "",
        "Could not find Visual Studio Build tools for UWP, please install it using Visual Studio Installer.");
}

#if defined(_WIN32)

namespace vcpkg::VisualStudio
{
    static constexpr StringLiteral V_120 = "v120";
    static constexpr StringLiteral V_140 = "v140";
    static constexpr StringLiteral V_141 = "v141";
    static constexpr StringLiteral V_142 = "v142";
    static constexpr StringLiteral V_143 = "v143";

    static constexpr StringLiteral WORKLOAD_NATIVE_DESKTOP = "Microsoft.VisualStudio.Workload.NativeDesktop";
    static constexpr StringLiteral WORKLOAD_CORE_FEATURES_TOOLS =
        "Microsoft.VisualStudio.ComponentGroup.NativeDesktop.Core";
    static constexpr StringLiteral WORKLOAD_MSBUILD = "Microsoft.Component.MSBuild";
    static constexpr StringLiteral COMPONENT_VCTOOLS_2015 = "Microsoft.VisualStudio.Component.VC.140";
    static constexpr StringLiteral COMPONENT_VCTOOLS_2017_OR_LATER =
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64";
    static constexpr StringLiteral COMPONENT_UCRT = "Microsoft.VisualStudio.Component.Windows10SDK";
    static constexpr StringLiteral COMPONENT_ARM_VCTOOLS = "Microsoft.VisualStudio.Component.VC.Tools.arm";
    static constexpr StringLiteral COMPONENT_ARM64_VCTOOLS = "Microsoft.VisualStudio.Component.VC.Tools.arm64";
    static constexpr StringLiteral COMPONENT_UWP = "Microsoft.VisualStudio.ComponentGroup.UWP.VC";

    static constexpr StringLiteral WINDOWS_SDKS[] = {"v8.1", "v10.0"};

    enum Installed_Component
    {
        none = 0,
        native_desktop = 1 << 0,
        have_core_features = 1 << 1,
        have_msbuild = 1 << 2,
        have_ucrt = 1 << 3,
        have_x86_x64_toolset = 1 << 4,
        have_arm_toolset = 1 << 5,
        have_arm64_toolset = 1 << 6,
        have_uwp_toolset = 1 << 7,
    };

    typedef unsigned char Components_Support;

    struct VisualStudioInstance
    {
        enum class ReleaseType
        {
            STABLE,
            PRERELEASE,
            LEGACY
        };

        static std::string release_type_to_string(const ReleaseType& release_type)
        {
            switch (release_type)
            {
                case ReleaseType::STABLE: return "STABLE";
                case ReleaseType::PRERELEASE: return "PRERELEASE";
                case ReleaseType::LEGACY: return "LEGACY";
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        static bool preferred_first_comparator(const VisualStudioInstance& left, const VisualStudioInstance& right)
        {
            const auto get_preference_weight = [](const ReleaseType& type) -> int {
                switch (type)
                {
                    case ReleaseType::STABLE: return 3;
                    case ReleaseType::PRERELEASE: return 2;
                    case ReleaseType::LEGACY: return 1;
                    default: Checks::unreachable(VCPKG_LINE_INFO);
                }
            };

            if (left.release_type != right.release_type)
            {
                return get_preference_weight(left.release_type) > get_preference_weight(right.release_type);
            }

            return left.version > right.version;
        }

        VisualStudioInstance(Path&& root_path,
                             std::string&& version,
                             const ReleaseType& release_type,
                             const Components_Support support = Installed_Component::none)
            : root_path(std::move(root_path))
            , version(std::move(version))
            , release_type(release_type)
            , component_support(support)
        {
        }

        Path root_path;
        std::string version;
        ReleaseType release_type;
        Components_Support component_support;

        std::string to_string() const
        {
            return Strings::format("%s, %s, %s", root_path, version, release_type_to_string(release_type));
        }

        std::string major_version() const { return version.substr(0, 2); }
    };

    static std::vector<VisualStudioInstance> get_visual_studio_instances_internal(const Filesystem& fs)
    {
        std::vector<VisualStudioInstance> instances;

        const auto& program_files_32_bit = get_program_files_32_bit().value_or_exit(VCPKG_LINE_INFO);

        // Instances from vswhere
        const Path vswhere_exe = program_files_32_bit / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        if (fs.exists(vswhere_exe, IgnoreErrors{}))
        {
            const auto code_and_output = cmd_execute_and_capture_output(Command(vswhere_exe)
                                                                            .string_arg("-all")
                                                                            .string_arg("-prerelease")
                                                                            .string_arg("-legacy")
                                                                            .string_arg("-products")
                                                                            .string_arg("*")
                                                                            .string_arg("-format")
                                                                            .string_arg("xml"));
            Checks::check_exit(VCPKG_LINE_INFO,
                               code_and_output.exit_code == 0,
                               "Running vswhere.exe failed with message:\n%s",
                               code_and_output.output);

            const auto instance_entries =
                Strings::find_all_enclosed(code_and_output.output, "<instance>", "</instance>");
            for (const StringView& instance : instance_entries)
            {
                auto maybe_is_prerelease =
                    Strings::find_at_most_one_enclosed(instance, "<isPrerelease>", "</isPrerelease>");

                VisualStudioInstance::ReleaseType release_type = VisualStudioInstance::ReleaseType::LEGACY;
                if (const auto p = maybe_is_prerelease.get())
                {
                    const auto s = p->to_string();
                    if (s == "0")
                        release_type = VisualStudioInstance::ReleaseType::STABLE;
                    else if (s == "1")
                        release_type = VisualStudioInstance::ReleaseType::PRERELEASE;
                    else
                        Checks::unreachable(VCPKG_LINE_INFO);
                }

                const auto& vs_version =
                    Strings::find_exactly_one_enclosed(instance, "<installationVersion>", "</installationVersion>")
                        .to_string();

                StringLiteral x86_64_toolset("");
                // For Visual Studio 2015
                if (0 == vs_version.compare(0, 3, "14."))
                {
                    x86_64_toolset = COMPONENT_VCTOOLS_2015;
                }
                // For Visual Studio 2017 or later
                else if (0 == vs_version.compare(0, 3, "15.") || 0 == vs_version.compare(0, 3, "16.") ||
                         0 == vs_version.compare(0, 3, "17."))
                {
                    x86_64_toolset = COMPONENT_VCTOOLS_2017_OR_LATER;
                }
                // For unknown Visual Studio version
                else
                {
                    print2("Unknown Visual Studio version: %s", vs_version);
                }

                Components_Support support = Installed_Component::none;
                if (x86_64_toolset != "")
                {
                    const auto general_output =
                        cmd_execute_and_capture_output(Command(vswhere_exe)
                                                           .string_arg("-latest")
                                                           .string_arg("-products")
                                                           .string_arg("*")
                                                           .string_arg("-requires")
                                                           .string_arg(WORKLOAD_NATIVE_DESKTOP)
                                                           .string_arg(WORKLOAD_CORE_FEATURES_TOOLS)
                                                           .string_arg(WORKLOAD_MSBUILD)
                                                           .string_arg(x86_64_toolset)
                                                           .string_arg(COMPONENT_UCRT)
                                                           .string_arg("-property")
                                                           .string_arg("installationVersion"));
                    Checks::check_exit(VCPKG_LINE_INFO,
                                       general_output.exit_code == 0,
                                       "Running vswhere.exe failed with message:\n%s",
                                       general_output.output);
                    if (!general_output.output.empty() &&
                        0 == general_output.output.compare(0, vs_version.length(), vs_version))
                    {
                        // VC++
                        const auto native_desktop_output =
                            cmd_execute_and_capture_output(Command(vswhere_exe)
                                                               .string_arg("-latest")
                                                               .string_arg("-products")
                                                               .string_arg("*")
                                                               .string_arg("-requires")
                                                               .string_arg(WORKLOAD_NATIVE_DESKTOP)
                                                               .string_arg("-property")
                                                               .string_arg("installationVersion"));
                        Checks::check_exit(VCPKG_LINE_INFO,
                                           native_desktop_output.exit_code == 0,
                                           "Running vswhere.exe failed with message:\n%s",
                                           native_desktop_output.output);

                        if (!native_desktop_output.output.empty() &&
                            0 == native_desktop_output.output.compare(0, vs_version.length(), vs_version))
                            support |= Installed_Component::native_desktop;

                        const auto core_features_output =
                            cmd_execute_and_capture_output(Command(vswhere_exe)
                                                               .string_arg("-latest")
                                                               .string_arg("-products")
                                                               .string_arg("*")
                                                               .string_arg("-requires")
                                                               .string_arg(WORKLOAD_CORE_FEATURES_TOOLS)
                                                               .string_arg("-property")
                                                               .string_arg("installationVersion"));
                        Checks::check_exit(VCPKG_LINE_INFO,
                                           core_features_output.exit_code == 0,
                                           "Running vswhere.exe failed with message:\n%s",
                                           core_features_output.output);

                        if (!core_features_output.output.empty() &&
                            0 == core_features_output.output.compare(0, vs_version.length(), vs_version))
                            support |= Installed_Component::have_core_features;

                        const auto msbuild_output =
                            cmd_execute_and_capture_output(Command(vswhere_exe)
                                                               .string_arg("-latest")
                                                               .string_arg("-products")
                                                               .string_arg("*")
                                                               .string_arg("-requires")
                                                               .string_arg(WORKLOAD_MSBUILD)
                                                               .string_arg("-property")
                                                               .string_arg("installationVersion"));
                        Checks::check_exit(VCPKG_LINE_INFO,
                                           msbuild_output.exit_code == 0,
                                           "Running vswhere.exe failed with message:\n%s",
                                           msbuild_output.output);

                        if (!msbuild_output.output.empty() &&
                            0 == msbuild_output.output.compare(0, vs_version.length(), vs_version))
                            support |= Installed_Component::have_msbuild;

                        const auto x86_x64_toolset_output =
                            cmd_execute_and_capture_output(Command(vswhere_exe)
                                                               .string_arg("-latest")
                                                               .string_arg("-products")
                                                               .string_arg("*")
                                                               .string_arg("-requires")
                                                               .string_arg(x86_64_toolset)
                                                               .string_arg("-property")
                                                               .string_arg("installationVersion"));
                        Checks::check_exit(VCPKG_LINE_INFO,
                                           x86_x64_toolset_output.exit_code == 0,
                                           "Running vswhere.exe failed with message:\n%s",
                                           x86_x64_toolset_output.output);

                        if (!x86_x64_toolset_output.output.empty() &&
                            0 == x86_x64_toolset_output.output.compare(0, vs_version.length(), vs_version))
                            support |= Installed_Component::have_x86_x64_toolset;

                        const auto ucrt_output = cmd_execute_and_capture_output(Command(vswhere_exe)
                                                                                    .string_arg("-latest")
                                                                                    .string_arg("-products")
                                                                                    .string_arg("*")
                                                                                    .string_arg("-requires")
                                                                                    .string_arg(COMPONENT_UCRT)
                                                                                    .string_arg("-property")
                                                                                    .string_arg("installationVersion"));
                        Checks::check_exit(VCPKG_LINE_INFO,
                                           ucrt_output.exit_code == 0,
                                           "Running vswhere.exe failed with message:\n%s",
                                           ucrt_output.output);

                        if (!ucrt_output.output.empty() &&
                            0 == ucrt_output.output.compare(0, vs_version.length(), vs_version))
                            support |= Installed_Component::have_ucrt;

                        // ARM
                        const auto arm_output = cmd_execute_and_capture_output(Command(vswhere_exe)
                                                                                   .string_arg("-latest")
                                                                                   .string_arg("-products")
                                                                                   .string_arg("*")
                                                                                   .string_arg("-requires")
                                                                                   .string_arg(COMPONENT_ARM_VCTOOLS)
                                                                                   .string_arg("-property")
                                                                                   .string_arg("installationVersion"));
                        Checks::check_exit(VCPKG_LINE_INFO,
                                           arm_output.exit_code == 0,
                                           "Running vswhere.exe failed with message:\n%s",
                                           arm_output.output);

                        if (!arm_output.output.empty() &&
                            0 == arm_output.output.compare(0, vs_version.length(), vs_version))
                            support |= Installed_Component::have_arm_toolset;

                        // ARM64
                        const auto arm64_output =
                            cmd_execute_and_capture_output(Command(vswhere_exe)
                                                               .string_arg("-latest")
                                                               .string_arg("-products")
                                                               .string_arg("*")
                                                               .string_arg("-requires")
                                                               .string_arg(COMPONENT_ARM64_VCTOOLS)
                                                               .string_arg("-property")
                                                               .string_arg("installationVersion"));
                        Checks::check_exit(VCPKG_LINE_INFO,
                                           arm64_output.exit_code == 0,
                                           "Running vswhere.exe failed with message:\n%s",
                                           arm64_output.output);

                        if (!arm64_output.output.empty() &&
                            0 == arm64_output.output.compare(0, vs_version.length(), vs_version))
                            support |= Installed_Component::have_arm64_toolset;

                        // UWP
                        const auto uwp_output = cmd_execute_and_capture_output(Command(vswhere_exe)
                                                                                   .string_arg("-latest")
                                                                                   .string_arg("-products")
                                                                                   .string_arg("*")
                                                                                   .string_arg("-requires")
                                                                                   .string_arg(COMPONENT_UWP)
                                                                                   .string_arg("-property")
                                                                                   .string_arg("installationVersion"));
                        Checks::check_exit(VCPKG_LINE_INFO,
                                           uwp_output.exit_code == 0,
                                           "Running vswhere.exe failed with message:\n%s",
                                           uwp_output.output);

                        if (!uwp_output.output.empty() &&
                            0 == uwp_output.output.compare(0, vs_version.length(), vs_version))
                            support |= Installed_Component::have_uwp_toolset;
                    }
                    else
                    {
                        support |= Installed_Component::native_desktop;
                        support |= Installed_Component::have_core_features;
                        support |= Installed_Component::have_msbuild;
                        support |= Installed_Component::have_x86_x64_toolset;
                        support |= Installed_Component::have_ucrt;
                        support |= Installed_Component::have_arm_toolset;
                        support |= Installed_Component::have_arm64_toolset;
                        support |= Installed_Component::have_uwp_toolset;
                    }
                }

                instances.emplace_back(
                    Strings::find_exactly_one_enclosed(instance, "<installationPath>", "</installationPath>")
                        .to_string(),
                    Strings::find_exactly_one_enclosed(instance, "<installationVersion>", "</installationVersion>")
                        .to_string(),
                    release_type,
                    support);
            }
        }

        const auto maybe_append_path = [&](Path&& path_root, ZStringView version, bool check_cl = true) {
            if (check_cl)
            {
                const auto cl_exe = path_root / "VC" / "bin" / "cl.exe";
                const auto vcvarsall_bat = path_root / "VC" / "vcvarsall.bat";

                if (!(fs.exists(cl_exe, IgnoreErrors{}) && fs.exists(vcvarsall_bat, IgnoreErrors{}))) return;
            }

            instances.emplace_back(std::move(path_root), version.c_str(), VisualStudioInstance::ReleaseType::LEGACY);
        };

        const auto maybe_append_comntools = [&](ZStringView env_var, ZStringView version, bool check_cl = true) {
            auto maybe_comntools = get_environment_variable(env_var);
            if (const auto path_as_string = maybe_comntools.get())
            {
                // We want lexically_normal(), but it is not available
                // Correct root path might be 2 or 3 levels up, depending on if the path has trailing backslash.
                Path common7_tools = *path_as_string;
                if (common7_tools.filename().empty())
                {
                    common7_tools = common7_tools.parent_path();
                }

                common7_tools = common7_tools.parent_path();
                common7_tools = common7_tools.parent_path();
                maybe_append_path(std::move(common7_tools), version, check_cl);
            }
        };

        const auto maybe_append_legacy_vs = [&](ZStringView env_var, const Path& dir, ZStringView version) {
            // VS instance from environment variable
            maybe_append_comntools(env_var, version);
            // VS instance from Program Files
            maybe_append_path(program_files_32_bit / dir, version);
        };

        // VS 2017 changed the installer such that cl.exe cannot be found by path navigation and
        // the env variable is only set when vcvars has been run. Therefore we close the safety valves.
        maybe_append_comntools("vs160comntools", "16.0", false);
        maybe_append_legacy_vs("vs140comntools", "Microsoft Visual Studio 14.0", "14.0");
        maybe_append_legacy_vs("vs120comntools", "Microsoft Visual Studio 12.0", "12.0");

        return instances;
    }

    static std::vector<ToolVersion> get_windows_sdk_versions()
    {
        std::vector<ToolVersion> toolVersions;

        for (auto current_sdk : WINDOWS_SDKS)
        {
            auto sdk_ver =
                vcpkg::get_registry_string(HKEY_LOCAL_MACHINE,
                                           R"(SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\)" + current_sdk,
                                           "ProductVersion");

            if (!sdk_ver.has_value())
                sdk_ver = vcpkg::get_registry_string(HKEY_CURRENT_USER,
                                                     R"(SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\)" +
                                                         current_sdk,
                                                     "ProductVersion");

            if (sdk_ver.has_value()) toolVersions.emplace_back(*sdk_ver.get());
        }

        return toolVersions;
    }

    std::vector<std::string> get_visual_studio_instances(const Filesystem& fs)
    {
        std::vector<VisualStudioInstance> sorted{get_visual_studio_instances_internal(fs)};
        std::sort(sorted.begin(), sorted.end(), VisualStudioInstance::preferred_first_comparator);
        return Util::fmap(sorted, [](const VisualStudioInstance& instance) { return instance.to_string(); });
    }

    ToolsetsInformation find_toolset_instances_preferred_first(const Filesystem& fs)
    {
        ToolsetsInformation ret;

        using CPU = CPUArchitecture;

        // Note: this will contain a mix of vcvarsall.bat locations and dumpbin.exe locations.
        std::vector<Path>& paths_examined = ret.paths_examined;
        std::vector<Toolset>& found_toolsets = ret.toolsets;
        std::vector<ToolVersion>& winsdk_versions = ret.winsdk_versions;

        const SortedVector<VisualStudioInstance, decltype(&VisualStudioInstance::preferred_first_comparator)> sorted{
            get_visual_studio_instances_internal(fs), VisualStudioInstance::preferred_first_comparator};

        const bool v140_is_available = Util::find_if(sorted, [&](const VisualStudioInstance& vs_instance) {
                                           return vs_instance.major_version() == "14";
                                       }) != sorted.end();

        for (const VisualStudioInstance& vs_instance : sorted)
        {
            const std::string major_version = vs_instance.major_version();
            if (major_version >= "15")
            {
                const auto vc_dir = vs_instance.root_path / "VC";

                // Skip any instances that do not have vcvarsall.
                const auto vcvarsall_dir = vc_dir / "Auxiliary/Build";
                const auto vcvarsall_bat = vcvarsall_dir / "vcvarsall.bat";
                paths_examined.push_back(vcvarsall_bat);
                if (!fs.exists(vcvarsall_bat, IgnoreErrors{})) continue;

                // Get all supported architectures
                std::vector<ToolsetArchOption> supported_architectures;
                bool have_basic_support = (vs_instance.component_support & Installed_Component::have_core_features) &&
                                          (vs_instance.component_support & Installed_Component::have_msbuild) &&
                                          (vs_instance.component_support & Installed_Component::native_desktop) &&
                                          (vs_instance.component_support & Installed_Component::have_ucrt);
                if (fs.exists(vcvarsall_dir / "vcvars32.bat", IgnoreErrors{}) && have_basic_support &&
                    (vs_instance.component_support & Installed_Component::have_x86_x64_toolset))
                    supported_architectures.push_back({"x86", CPU::X86, CPU::X86});
                if (fs.exists(vcvarsall_dir / "vcvars64.bat", IgnoreErrors{}) && have_basic_support &&
                    (vs_instance.component_support & Installed_Component::have_x86_x64_toolset))
                    supported_architectures.push_back({"amd64", CPU::X64, CPU::X64});
                if (fs.exists(vcvarsall_dir / "vcvarsx86_amd64.bat", IgnoreErrors{}) && have_basic_support &&
                    (vs_instance.component_support & Installed_Component::have_x86_x64_toolset))
                    supported_architectures.push_back({"x86_amd64", CPU::X86, CPU::X64});
                if (fs.exists(vcvarsall_dir / "vcvarsx86_arm.bat", IgnoreErrors{}) && have_basic_support &&
                    (vs_instance.component_support & Installed_Component::have_arm_toolset))
                    supported_architectures.push_back({"x86_arm", CPU::X86, CPU::ARM});
                if (fs.exists(vcvarsall_dir / "vcvarsx86_arm64.bat", IgnoreErrors{}) && have_basic_support &&
                    (vs_instance.component_support & Installed_Component::have_arm64_toolset))
                    supported_architectures.push_back({"x86_arm64", CPU::X86, CPU::ARM64});
                if (fs.exists(vcvarsall_dir / "vcvarsamd64_x86.bat", IgnoreErrors{}) && have_basic_support &&
                    (vs_instance.component_support & Installed_Component::have_x86_x64_toolset))
                    supported_architectures.push_back({"amd64_x86", CPU::X64, CPU::X86});
                if (fs.exists(vcvarsall_dir / "vcvarsamd64_arm.bat", IgnoreErrors{}) && have_basic_support &&
                    (vs_instance.component_support & Installed_Component::have_arm_toolset))
                    supported_architectures.push_back({"amd64_arm", CPU::X64, CPU::ARM});
                if (fs.exists(vcvarsall_dir / "vcvarsamd64_arm64.bat", IgnoreErrors{}) && have_basic_support &&
                    (vs_instance.component_support & Installed_Component::have_arm64_toolset))
                    supported_architectures.push_back({"amd64_arm64", CPU::X64, CPU::ARM64});

                // Locate the "best" MSVC toolchain version
                const auto msvc_path = vc_dir / "Tools/MSVC";
                std::vector<Path> msvc_subdirectories = fs.get_directories_non_recursive(msvc_path, IgnoreErrors{});

                // Sort them so that latest comes first
                std::sort(msvc_subdirectories.begin(),
                          msvc_subdirectories.end(),
                          [](const Path& left, const Path& right) { return left.filename() > right.filename(); });

                for (const Path& subdir : msvc_subdirectories)
                {
                    auto toolset_version_full = subdir.filename();
                    auto toolset_version_prefix = toolset_version_full.substr(0, 4);
                    ZStringView toolset_version;
                    std::string vcvars_option = "-vcvars_ver=" + toolset_version_full.to_string();
                    if (toolset_version_prefix.size() != 4)
                    {
                        // unknown toolset
                        continue;
                    }
                    else if (toolset_version_prefix[3] == '1')
                    {
                        toolset_version = V_141;
                    }
                    else if (toolset_version_prefix[3] == '2')
                    {
                        toolset_version = V_142;
                    }
                    else if (toolset_version_prefix[3] == '3')
                    {
                        toolset_version = V_143;
                    }
                    else
                    {
                        // unknown toolset minor version
                        continue;
                    }
                    const auto dumpbin_dir = subdir / "bin/HostX86/x86";
                    const auto dumpbin_path = dumpbin_dir / "dumpbin.exe";
                    paths_examined.push_back(dumpbin_path);
                    if (fs.exists(dumpbin_path, IgnoreErrors{}))
                    {
                        Toolset toolset{vs_instance.root_path,
                                        dumpbin_path,
                                        vcvarsall_bat,
                                        {vcvars_option},
                                        toolset_version,
                                        toolset_version_full.to_string(),
                                        supported_architectures};

                        found_toolsets.push_back(std::move(toolset));
                        if (v140_is_available)
                        {
                            found_toolsets.push_back({vs_instance.root_path,
                                                      dumpbin_path,
                                                      vcvarsall_bat,
                                                      {"-vcvars_ver=14.0"},
                                                      V_140,
                                                      "14",
                                                      supported_architectures});
                        }

                        continue;
                    }
                }

                continue;
            }

            if (major_version == "14" || major_version == "12")
            {
                const auto vcvarsall_bat = vs_instance.root_path / "VC/vcvarsall.bat";

                paths_examined.push_back(vcvarsall_bat);
                if (fs.exists(vcvarsall_bat, IgnoreErrors{}))
                {
                    const auto vs_dumpbin_dir = vs_instance.root_path / "VC/bin";
                    const auto vs_dumpbin_exe = vs_dumpbin_dir / "dumpbin.exe";
                    paths_examined.push_back(vs_dumpbin_exe);

                    const auto vs_bin_dir = Path(vcvarsall_bat.parent_path()) / "bin";
                    std::vector<ToolsetArchOption> supported_architectures;
                    if (fs.exists(vs_bin_dir / "vcvars32.bat", IgnoreErrors{}))
                        supported_architectures.push_back({"x86", CPU::X86, CPU::X86});
                    if (fs.exists(vs_bin_dir / "amd64/vcvars64.bat", IgnoreErrors{}))
                        supported_architectures.push_back({"x64", CPU::X64, CPU::X64});
                    if (fs.exists(vs_bin_dir / "x86_amd64/vcvarsx86_amd64.bat", IgnoreErrors{}))
                        supported_architectures.push_back({"x86_amd64", CPU::X86, CPU::X64});
                    if (fs.exists(vs_bin_dir / "x86_arm/vcvarsx86_arm.bat", IgnoreErrors{}))
                        supported_architectures.push_back({"x86_arm", CPU::X86, CPU::ARM});
                    if (fs.exists(vs_bin_dir / "amd64_x86/vcvarsamd64_x86.bat", IgnoreErrors{}))
                        supported_architectures.push_back({"amd64_x86", CPU::X64, CPU::X86});
                    if (fs.exists(vs_bin_dir / "amd64_arm/vcvarsamd64_arm.bat", IgnoreErrors{}))
                        supported_architectures.push_back({"amd64_arm", CPU::X64, CPU::ARM});

                    if (fs.exists(vs_dumpbin_exe, IgnoreErrors{}))
                    {
                        const Toolset toolset = {vs_instance.root_path,
                                                 vs_dumpbin_exe,
                                                 vcvarsall_bat,
                                                 {},
                                                 major_version == "14" ? V_140 : V_120,
                                                 major_version,
                                                 supported_architectures};

                        found_toolsets.push_back(toolset);
                    }
                }
            }
        }

        winsdk_versions = get_windows_sdk_versions();

        return ret;
    }
}
namespace vcpkg
{
    LocalizedString ToolsetsInformation::get_localized_debug_info() const
    {
        LocalizedString ret;

        if (toolsets.empty())
        {
            ret.append(msgVSNoInstances).appendnl();
        }
        else
        {
            ret.append(msgVSExaminedInstances).appendnl();
            for (const Toolset& toolset : toolsets)
            {
                ret.append_indent().append_raw(toolset.visual_studio_root_path).appendnl();
            }
        }

        if (!paths_examined.empty())
        {
            ret.append(msgVSExaminedPaths).appendnl();
            for (const Path& examinee : paths_examined)
            {
                ret.append_indent().append_raw(examinee).appendnl();
            }
        }

        if (winsdk_versions.empty())
        {
            ret.append(msg::format(msgVSNoWindowsSDK)).appendnl();
        }

        return ret;
    }
}

#endif
