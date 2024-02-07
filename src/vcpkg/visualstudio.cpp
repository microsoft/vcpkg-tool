#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/visualstudio.h>

namespace
{
    using namespace vcpkg;

    constexpr StringLiteral V_120 = "v120";
    constexpr StringLiteral V_140 = "v140";
    constexpr StringLiteral V_141 = "v141";
    constexpr StringLiteral V_142 = "v142";
    constexpr StringLiteral V_143 = "v143";
} // unnamed namespace

namespace vcpkg::VisualStudio
{
    StringLiteral to_string_literal(ReleaseType release_type) noexcept
    {
        switch (release_type)
        {
            case ReleaseType::STABLE: return "STABLE";
            case ReleaseType::PRERELEASE: return "PRERELEASE";
            case ReleaseType::LEGACY: return "LEGACY";
            case ReleaseType::UNKNOWN: return "UNKNOWN";
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    VisualStudioInstance::VisualStudioInstance(Path&& root_path, std::string&& version, ReleaseType release_type)
        : root_path(std::move(root_path)), version(std::move(version)), release_type(release_type)
    {
    }

    std::string VisualStudioInstance::to_string() const
    {
        return fmt::format("{}, {}, {}", root_path, version, release_type);
    }

    std::string VisualStudioInstance::major_version() const { return version.substr(0, 2); }

#if defined(_WIN32)
    static std::vector<VisualStudioInstance> get_visual_studio_instances_internal(const ReadOnlyFilesystem& fs)
    {
        std::vector<VisualStudioInstance> instances;

        const auto& maybe_program_files_32_bit = get_program_files_32_bit();
        if (auto program_files_32_bit = maybe_program_files_32_bit.get())
        {
            const Path vswhere_exe = *program_files_32_bit / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
            if (fs.exists(vswhere_exe, IgnoreErrors{}))
            {
                auto maybe_output = flatten_out(cmd_execute_and_capture_output(Command(vswhere_exe)
                                                                                   .string_arg("-all")
                                                                                   .string_arg("-prerelease")
                                                                                   .string_arg("-legacy")
                                                                                   .string_arg("-products")
                                                                                   .string_arg("*")
                                                                                   .string_arg("-format")
                                                                                   .string_arg("xml")),
                                                "vswhere");
                if (auto output = maybe_output.get())
                {
                    const auto instance_entries = Strings::find_all_enclosed(*output, "<instance>", "</instance>");
                    for (const StringView& instance : instance_entries)
                    {
                        auto maybe_is_prerelease =
                            Strings::find_at_most_one_enclosed(instance, "<isPrerelease>", "</isPrerelease>");

                        ReleaseType release_type = ReleaseType::LEGACY;
                        if (const auto p = maybe_is_prerelease.get())
                        {
                            const auto s = p->to_string();
                            if (s == "0")
                            {
                                release_type = ReleaseType::STABLE;
                            }
                            else if (s == "1")
                            {
                                release_type = ReleaseType::PRERELEASE;
                            }
                            else
                            {
                                release_type = ReleaseType::UNKNOWN;
                            }
                        }

                        instances.emplace_back(
                            Strings::find_exactly_one_enclosed(instance, "<installationPath>", "</installationPath>")
                                .to_string(),
                            Strings::find_exactly_one_enclosed(
                                instance, "<installationVersion>", "</installationVersion>")
                                .to_string(),
                            release_type);
                    }
                }
                else
                {
                    Debug::println("vswhere failed, skipping modern VS detection");
                }
            }
        }

        const auto maybe_append_path = [&](Path&& path_root, ZStringView version, bool check_cl) {
            if (check_cl)
            {
                const auto cl_exe = path_root / "VC" / "bin" / "cl.exe";
                const auto vcvarsall_bat = path_root / "VC" / "vcvarsall.bat";

                if (!(fs.exists(cl_exe, IgnoreErrors{}) && fs.exists(vcvarsall_bat, IgnoreErrors{}))) return;
            }

            instances.emplace_back(std::move(path_root), version.c_str(), ReleaseType::LEGACY);
        };

        const auto maybe_append_comntools = [&](ZStringView env_var, ZStringView version, bool check_cl) {
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

        const auto maybe_append_legacy_vs = [&](StringLiteral env_var, const Path& dir, StringLiteral version) {
            // VS instance from environment variable
            maybe_append_comntools(env_var, version, true);
            // VS instance from Program Files
            if (auto program_files_32_bit = maybe_program_files_32_bit.get())
            {
                maybe_append_path(*program_files_32_bit / dir, version, true);
            }
        };

        // VS 2017 changed the installer such that cl.exe cannot be found by path navigation and
        // the env variable is only set when vcvars has been run. Therefore we close the safety valves.
        maybe_append_comntools("vs160comntools", "16.0", false);
        maybe_append_legacy_vs("vs140comntools", "Microsoft Visual Studio 14.0", "14.0");
        maybe_append_legacy_vs("vs120comntools", "Microsoft Visual Studio 12.0", "12.0");

        return instances;
    }

    std::vector<VisualStudioInstance> get_sorted_visual_studio_instances(const ReadOnlyFilesystem& fs)
    {
        std::vector<VisualStudioInstance> sorted{get_visual_studio_instances_internal(fs)};
        std::sort(
            sorted.begin(), sorted.end(), [](const VisualStudioInstance& left, const VisualStudioInstance& right) {
                if (left.release_type != right.release_type)
                {
                    return static_cast<int>(left.release_type) < static_cast<int>(right.release_type);
                }

                return left.version > right.version;
            });
        return sorted;
    }

    ToolsetsInformation find_toolset_instances_preferred_first(
        const ReadOnlyFilesystem& fs, const std::vector<VisualStudioInstance>& sorted_visual_studio_instances)
    {
        ToolsetsInformation ret;

        using CPU = CPUArchitecture;

        // Note: this will contain a mix of vcvarsall.bat locations and dumpbin.exe locations.
        std::vector<Path>& paths_examined = ret.paths_examined;
        std::vector<Toolset>& found_toolsets = ret.toolsets;

        const bool v140_is_available =
            Util::find_if(sorted_visual_studio_instances, [&](const VisualStudioInstance& vs_instance) {
                return vs_instance.major_version() == "14";
            }) != sorted_visual_studio_instances.end();

        for (const VisualStudioInstance& vs_instance : sorted_visual_studio_instances)
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
                if (fs.exists(vcvarsall_dir / "vcvars32.bat", IgnoreErrors{}))
                    supported_architectures.push_back({"x86", CPU::X86, CPU::X86});
                if (fs.exists(vcvarsall_dir / "vcvars64.bat", IgnoreErrors{}))
                    supported_architectures.push_back({"amd64", CPU::X64, CPU::X64});
                // Host x86
                if (fs.exists(vcvarsall_dir / "vcvarsx86_amd64.bat", IgnoreErrors{}))
                    supported_architectures.push_back({"x86_amd64", CPU::X86, CPU::X64});
                if (fs.exists(vcvarsall_dir / "vcvarsx86_arm.bat", IgnoreErrors{}))
                    supported_architectures.push_back({"x86_arm", CPU::X86, CPU::ARM});
                if (fs.exists(vcvarsall_dir / "vcvarsx86_arm64.bat", IgnoreErrors{}))
                    supported_architectures.push_back({"x86_arm64", CPU::X86, CPU::ARM64});
                // Host x64
                if (fs.exists(vcvarsall_dir / "vcvarsamd64_x86.bat", IgnoreErrors{}))
                    supported_architectures.push_back({"amd64_x86", CPU::X64, CPU::X86});
                if (fs.exists(vcvarsall_dir / "vcvarsamd64_arm.bat", IgnoreErrors{}))
                    supported_architectures.push_back({"amd64_arm", CPU::X64, CPU::ARM});
                if (fs.exists(vcvarsall_dir / "vcvarsamd64_arm64.bat", IgnoreErrors{}))
                    supported_architectures.push_back({"amd64_arm64", CPU::X64, CPU::ARM64});
                // Host arm64
                if (fs.exists(vcvarsall_dir / "vcvarsarm64.bat", IgnoreErrors{}))
                    supported_architectures.push_back({"arm64", CPU::ARM64, CPU::ARM64});
                if (fs.exists(vcvarsall_dir / "vcvarsarm64_x86.bat", IgnoreErrors{}))
                    supported_architectures.push_back({"arm64_x86", CPU::ARM64, CPU::X86});
                if (fs.exists(vcvarsall_dir / "vcvarsarm64_amd64.bat", IgnoreErrors{}))
                    supported_architectures.push_back({"arm64_amd64", CPU::ARM64, CPU::X64});

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
                    else if (toolset_version_prefix[3] == '3' || toolset_version_prefix[3] == '4')
                    {
                        toolset_version = V_143;
                    }
                    else
                    {
                        // unknown toolset minor version
                        continue;
                    }

                    Toolset toolset{vs_instance.root_path,
                                    vcvarsall_bat,
                                    {vcvars_option},
                                    toolset_version,
                                    toolset_version_full.to_string(),
                                    supported_architectures};

                    found_toolsets.push_back(std::move(toolset));
                    if (v140_is_available)
                    {
                        found_toolsets.push_back({vs_instance.root_path,
                                                  vcvarsall_bat,
                                                  {"-vcvars_ver=14.0"},
                                                  V_140,
                                                  "14",
                                                  supported_architectures});
                    }

                    continue;
                }

                continue;
            }

            if (major_version == "14" || major_version == "12")
            {
                const auto vcvarsall_bat = vs_instance.root_path / "VC/vcvarsall.bat";
                paths_examined.push_back(vcvarsall_bat);
                if (fs.exists(vcvarsall_bat, IgnoreErrors{}))
                {
                    const auto vs_bin_dir = Path(vcvarsall_bat.parent_path()) / "bin";
                    std::vector<ToolsetArchOption> supported_architectures;
                    if (fs.exists(vs_bin_dir / "vcvars32.bat", IgnoreErrors{}))
                    {
                        supported_architectures.push_back({"x86", CPU::X86, CPU::X86});
                    }

                    if (fs.exists(vs_bin_dir / "amd64/vcvars64.bat", IgnoreErrors{}))
                    {
                        supported_architectures.push_back({"x64", CPU::X64, CPU::X64});
                    }

                    if (fs.exists(vs_bin_dir / "x86_amd64/vcvarsx86_amd64.bat", IgnoreErrors{}))
                    {
                        supported_architectures.push_back({"x86_amd64", CPU::X86, CPU::X64});
                    }

                    if (fs.exists(vs_bin_dir / "x86_arm/vcvarsx86_arm.bat", IgnoreErrors{}))
                    {
                        supported_architectures.push_back({"x86_arm", CPU::X86, CPU::ARM});
                    }

                    if (fs.exists(vs_bin_dir / "amd64_x86/vcvarsamd64_x86.bat", IgnoreErrors{}))
                    {
                        supported_architectures.push_back({"amd64_x86", CPU::X64, CPU::X86});
                    }

                    if (fs.exists(vs_bin_dir / "amd64_arm/vcvarsamd64_arm.bat", IgnoreErrors{}))
                    {
                        supported_architectures.push_back({"amd64_arm", CPU::X64, CPU::ARM});
                    }

                    found_toolsets.push_back(Toolset{vs_instance.root_path,
                                                     vcvarsall_bat,
                                                     {},
                                                     major_version == "14" ? V_140 : V_120,
                                                     major_version,
                                                     supported_architectures});
                }
            }
        }

        return ret;
    }
#else  // ^^^ _WIN32 // !_WIN32 vvv
    std::vector<VisualStudioInstance> get_sorted_visual_studio_instances(const ReadOnlyFilesystem&)
    {
        return std::vector<VisualStudioInstance>{};
    }

    ToolsetsInformation find_toolset_instances_preferred_first(const ReadOnlyFilesystem&,
                                                               const std::vector<VisualStudioInstance>&)
    {
        return ToolsetsInformation{};
    }
#endif // ^^^ !_WIN32
} // namespace vcpkg::VisualStudio
