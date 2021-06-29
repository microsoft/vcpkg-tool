#if defined(_WIN32)

#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/visualstudio.h>

namespace vcpkg::VisualStudio
{
    static constexpr CStringView V_120 = "v120";
    static constexpr CStringView V_140 = "v140";
    static constexpr CStringView V_141 = "v141";
    static constexpr CStringView V_142 = "v142";

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

        VisualStudioInstance(path&& root_path, std::string&& version, const ReleaseType& release_type)
            : root_path(std::move(root_path)), version(std::move(version)), release_type(release_type)
        {
        }

        path root_path;
        std::string version;
        ReleaseType release_type;

        std::string to_string() const
        {
            return Strings::format(
                "%s, %s, %s", vcpkg::u8string(root_path), version, release_type_to_string(release_type));
        }

        std::string major_version() const { return version.substr(0, 2); }
    };

    static std::vector<VisualStudioInstance> get_visual_studio_instances_internal(const VcpkgPaths& paths)
    {
        const auto& fs = paths.get_filesystem();
        std::vector<VisualStudioInstance> instances;

        const auto& program_files_32_bit = get_program_files_32_bit().value_or_exit(VCPKG_LINE_INFO);

        // Instances from vswhere
        const path vswhere_exe = program_files_32_bit / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        if (fs.exists(vswhere_exe))
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

                instances.emplace_back(
                    Strings::find_exactly_one_enclosed(instance, "<installationPath>", "</installationPath>")
                        .to_string(),
                    Strings::find_exactly_one_enclosed(instance, "<installationVersion>", "</installationVersion>")
                        .to_string(),
                    release_type);
            }
        }

        const auto maybe_append_path = [&](path&& path_root, CStringView version, bool check_cl = true) {
            if (check_cl)
            {
                const auto cl_exe = path_root / "VC" / "bin" / "cl.exe";
                const auto vcvarsall_bat = path_root / "VC" / "vcvarsall.bat";

                if (!(fs.exists(cl_exe) && fs.exists(vcvarsall_bat))) return;
            }

            instances.emplace_back(std::move(path_root), version.c_str(), VisualStudioInstance::ReleaseType::LEGACY);
        };

        const auto maybe_append_comntools = [&](ZStringView env_var, CStringView version, bool check_cl = true) {
            auto maybe_comntools = get_environment_variable(env_var);
            if (const auto path_as_string = maybe_comntools.get())
            {
                // We want lexically_normal(), but it is not available
                // Correct root path might be 2 or 3 levels up, depending on if the path has trailing backslash.
                auto common7_tools = vcpkg::u8path(*path_as_string);
                if (common7_tools.filename().empty())
                    maybe_append_path(common7_tools.parent_path().parent_path().parent_path(), version, check_cl);
                else
                    maybe_append_path(common7_tools.parent_path().parent_path(), version, check_cl);
            }
        };

        const auto maybe_append_legacy_vs = [&](ZStringView env_var, const path& dir, CStringView version) {
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

    std::vector<std::string> get_visual_studio_instances(const VcpkgPaths& paths)
    {
        std::vector<VisualStudioInstance> sorted{get_visual_studio_instances_internal(paths)};
        std::sort(sorted.begin(), sorted.end(), VisualStudioInstance::preferred_first_comparator);
        return Util::fmap(sorted, [](const VisualStudioInstance& instance) { return instance.to_string(); });
    }

    std::vector<Toolset> find_toolset_instances_preferred_first(const VcpkgPaths& paths)
    {
        using CPU = CPUArchitecture;

        const auto& fs = paths.get_filesystem();

        // Note: this will contain a mix of vcvarsall.bat locations and dumpbin.exe locations.
        std::vector<path> paths_examined;

        std::vector<Toolset> found_toolsets;
        std::vector<Toolset> excluded_toolsets;

        const SortedVector<VisualStudioInstance> sorted{get_visual_studio_instances_internal(paths),
                                                        VisualStudioInstance::preferred_first_comparator};

        const bool v140_is_available = Util::find_if(sorted, [&](const VisualStudioInstance& vs_instance) {
                                           return vs_instance.major_version() == "14";
                                       }) != sorted.end();

        for (const VisualStudioInstance& vs_instance : sorted)
        {
            const std::string major_version = vs_instance.major_version();
            if (major_version >= "15")
            {
                const path vc_dir = vs_instance.root_path / "VC";

                // Skip any instances that do not have vcvarsall.
                const path vcvarsall_dir = vc_dir / "Auxiliary" / "Build";
                const path vcvarsall_bat = vcvarsall_dir / "vcvarsall.bat";
                paths_examined.push_back(vcvarsall_bat);
                if (!fs.exists(vcvarsall_bat)) continue;

                // Get all supported architectures
                std::vector<ToolsetArchOption> supported_architectures;
                if (fs.exists(vcvarsall_dir / "vcvars32.bat"))
                    supported_architectures.push_back({"x86", CPU::X86, CPU::X86});
                if (fs.exists(vcvarsall_dir / "vcvars64.bat"))
                    supported_architectures.push_back({"amd64", CPU::X64, CPU::X64});
                if (fs.exists(vcvarsall_dir / "vcvarsx86_amd64.bat"))
                    supported_architectures.push_back({"x86_amd64", CPU::X86, CPU::X64});
                if (fs.exists(vcvarsall_dir / "vcvarsx86_arm.bat"))
                    supported_architectures.push_back({"x86_arm", CPU::X86, CPU::ARM});
                if (fs.exists(vcvarsall_dir / "vcvarsx86_arm64.bat"))
                    supported_architectures.push_back({"x86_arm64", CPU::X86, CPU::ARM64});
                if (fs.exists(vcvarsall_dir / "vcvarsamd64_x86.bat"))
                    supported_architectures.push_back({"amd64_x86", CPU::X64, CPU::X86});
                if (fs.exists(vcvarsall_dir / "vcvarsamd64_arm.bat"))
                    supported_architectures.push_back({"amd64_arm", CPU::X64, CPU::ARM});
                if (fs.exists(vcvarsall_dir / "vcvarsamd64_arm64.bat"))
                    supported_architectures.push_back({"amd64_arm64", CPU::X64, CPU::ARM64});

                // Locate the "best" MSVC toolchain version
                const path msvc_path = vc_dir / "Tools" / "MSVC";
                std::vector<path> msvc_subdirectories = fs.get_files_non_recursive(msvc_path);
                Util::erase_remove_if(msvc_subdirectories,
                                      [&fs](const path& target) { return !fs.is_directory(target); });

                // Sort them so that latest comes first
                std::sort(msvc_subdirectories.begin(),
                          msvc_subdirectories.end(),
                          [](const path& left, const path& right) { return left.filename() > right.filename(); });

                for (const path& subdir : msvc_subdirectories)
                {
                    auto toolset_version_full = vcpkg::u8string(subdir.filename());
                    auto toolset_version_prefix = toolset_version_full.substr(0, 4);
                    CStringView toolset_version;
                    std::string vcvars_option;
                    if (toolset_version_prefix.size() != 4)
                    {
                        // unknown toolset
                        continue;
                    }
                    else if (toolset_version_prefix[3] == '1')
                    {
                        toolset_version = V_141;
                        vcvars_option = "-vcvars_ver=14.1";
                    }
                    else if (toolset_version_prefix[3] == '2')
                    {
                        toolset_version = V_142;
                        vcvars_option = "-vcvars_ver=14.2";
                    }
                    else
                    {
                        // unknown toolset minor version
                        continue;
                    }
                    const path dumpbin_path = subdir / "bin" / "HostX86" / "x86" / "dumpbin.exe";
                    paths_examined.push_back(dumpbin_path);
                    if (fs.exists(dumpbin_path))
                    {
                        Toolset toolset{vs_instance.root_path,
                                        dumpbin_path,
                                        vcvarsall_bat,
                                        {vcvars_option},
                                        toolset_version,
                                        supported_architectures};

                        const auto english_language_pack = dumpbin_path.parent_path() / "1033";

                        if (!fs.exists(english_language_pack))
                        {
                            excluded_toolsets.push_back(std::move(toolset));
                            continue;
                        }

                        found_toolsets.push_back(std::move(toolset));

                        if (v140_is_available)
                        {
                            found_toolsets.push_back({vs_instance.root_path,
                                                      dumpbin_path,
                                                      vcvarsall_bat,
                                                      {"-vcvars_ver=14.0"},
                                                      V_140,
                                                      supported_architectures});
                        }

                        continue;
                    }
                }

                continue;
            }

            if (major_version == "14" || major_version == "12")
            {
                const path vcvarsall_bat = vs_instance.root_path / "VC" / "vcvarsall.bat";

                paths_examined.push_back(vcvarsall_bat);
                if (fs.exists(vcvarsall_bat))
                {
                    const path vs_dumpbin_exe = vs_instance.root_path / "VC" / "bin" / "dumpbin.exe";
                    paths_examined.push_back(vs_dumpbin_exe);

                    const path vs_bin_dir = vcvarsall_bat.parent_path() / "bin";
                    std::vector<ToolsetArchOption> supported_architectures;
                    if (fs.exists(vs_bin_dir / "vcvars32.bat"))
                        supported_architectures.push_back({"x86", CPU::X86, CPU::X86});
                    if (fs.exists(vs_bin_dir / "amd64\\vcvars64.bat"))
                        supported_architectures.push_back({"x64", CPU::X64, CPU::X64});
                    if (fs.exists(vs_bin_dir / "x86_amd64\\vcvarsx86_amd64.bat"))
                        supported_architectures.push_back({"x86_amd64", CPU::X86, CPU::X64});
                    if (fs.exists(vs_bin_dir / "x86_arm\\vcvarsx86_arm.bat"))
                        supported_architectures.push_back({"x86_arm", CPU::X86, CPU::ARM});
                    if (fs.exists(vs_bin_dir / "amd64_x86\\vcvarsamd64_x86.bat"))
                        supported_architectures.push_back({"amd64_x86", CPU::X64, CPU::X86});
                    if (fs.exists(vs_bin_dir / "amd64_arm\\vcvarsamd64_arm.bat"))
                        supported_architectures.push_back({"amd64_arm", CPU::X64, CPU::ARM});

                    if (fs.exists(vs_dumpbin_exe))
                    {
                        const Toolset toolset = {vs_instance.root_path,
                                                 vs_dumpbin_exe,
                                                 vcvarsall_bat,
                                                 {},
                                                 major_version == "14" ? V_140 : V_120,
                                                 supported_architectures};

                        const auto english_language_pack = vs_dumpbin_exe.parent_path() / "1033";

                        if (!fs.exists(english_language_pack))
                        {
                            excluded_toolsets.push_back(toolset);
                            break;
                        }

                        found_toolsets.push_back(toolset);
                    }
                }
            }
        }

        if (!excluded_toolsets.empty())
        {
            print2(
                Color::warning,
                "Warning: The following VS instances are excluded because the English language pack is unavailable.\n");
            for (const Toolset& toolset : excluded_toolsets)
            {
                print2("    ", vcpkg::u8string(toolset.visual_studio_root_path), '\n');
            }
            print2(Color::warning, "Please install the English language pack.\n");
        }

        if (found_toolsets.empty() && Debug::g_debugging)
        {
            Debug::print("Could not locate a complete Visual Studio instance\n");
            Debug::print("The following paths were examined:\n");
            for (const path& examinee : paths_examined)
            {
                Debug::print("    ", vcpkg::u8string(examinee), '\n');
            }
        }

        return found_toolsets;
    }
}

#endif
