#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/export.prefab.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Prefab
{
    static std::vector<Path> find_modules(const VcpkgPaths& system, const Path& root, const std::string& ext)
    {
        const Filesystem& fs = system.get_filesystem();
        std::error_code ec;
        auto paths = fs.get_regular_files_recursive(root, ec);
        if (ec)
        {
            if (ec == std::errc::not_a_directory || ec == std::errc::no_such_file_or_directory)
            {
                return paths;
            }
            exit_filesystem_call_error(VCPKG_LINE_INFO, ec, "find_modules", {root, ec.message()});
        }

        Util::erase_remove_if(paths, NotExtensionCaseSensitive{ext});
        return paths;
    }

    std::string NdkVersion::to_string() const
    {
        std::string ret;
        this->to_string(ret);
        return ret;
    }
    void NdkVersion::to_string(std::string& out) const
    {
        out.append("NdkVersion{major=")
            .append(std::to_string(major()))
            .append(",minor=")
            .append(std::to_string(minor()))
            .append(",patch=")
            .append(std::to_string(patch()))
            .append("}");
    }

    static std::string jsonify(const std::vector<std::string>& dependencies)
    {
        std::vector<std::string> deps;
        deps.reserve(dependencies.size());
        for (const auto& dep : dependencies)
        {
            deps.push_back("\"" + dep + "\"");
        }
        return Strings::join(",", deps);
    }

    static std::string null_if_empty(const std::string& str)
    {
        std::string copy = str;
        if (copy.empty())
        {
            copy = "null";
        }
        else
        {
            copy = "\"" + copy + "\"";
        }
        return copy;
    }

    static std::string null_if_empty_array(const std::string& str)
    {
        std::string copy = str;
        if (copy.empty())
        {
            copy = "null";
        }
        else
        {
            copy = "[" + copy + "]";
        }
        return copy;
    }

    std::string ABIMetadata::to_string() const
    {
        std::string TEMPLATE = R"({
    "abi":"@ABI@",
    "api":@API@,
    "ndk":@NDK@,
    "stl":"@STL@"
})";
        std::string json = Strings::replace_all(std::move(TEMPLATE), "@ABI@", abi);
        Strings::inplace_replace_all(json, "@API@", std::to_string(api));
        Strings::inplace_replace_all(json, "@NDK@", std::to_string(ndk));
        Strings::inplace_replace_all(json, "@STL@", stl);
        return json;
    }

    std::string PlatformModuleMetadata::to_json() const
    {
        std::string TEMPLATE = R"({
    "export_libraries": @LIBRARIES@,
    "library_name": @LIBRARY_NAME@
})";

        std::string json = Strings::replace_all(std::move(TEMPLATE), "@LIBRARY_NAME@", null_if_empty(library_name));
        Strings::inplace_replace_all(json, "@LIBRARIES@", null_if_empty_array(jsonify(export_libraries)));
        return json;
    }

    std::string ModuleMetadata::to_json() const
    {
        std::string TEMPLATE = R"({
    "export_libraries": [@LIBRARIES@],
    "library_name":@LIBRARY_NAME@,
    "android": @ANDROID_METADATA@
})";

        std::string json = Strings::replace_all(std::move(TEMPLATE), "@LIBRARY_NAME@", null_if_empty(library_name));
        Strings::inplace_replace_all(json, "@LIBRARIES@", jsonify(export_libraries));
        Strings::inplace_replace_all(json, "@ANDROID_METADATA@", android.to_json());
        return json;
    }

    std::string PackageMetadata::to_json() const
    {
        std::string deps = jsonify(dependencies);

        std::string TEMPLATE = R"({
    "name":"@PACKAGE_NAME@",
    "schema_version": @PACKAGE_SCHEMA@,
    "dependencies":[@PACKAGE_DEPS@],
    "version":"@PACKAGE_VERSION@"
})";
        std::string json = Strings::replace_all(std::move(TEMPLATE), "@PACKAGE_NAME@", name);
        Strings::inplace_replace_all(json, "@PACKAGE_SCHEMA@", std::to_string(schema));
        Strings::inplace_replace_all(json, "@PACKAGE_DEPS@", deps);
        Strings::inplace_replace_all(json, "@PACKAGE_VERSION@", version);
        return json;
    }

    Optional<StringView> find_ndk_version(StringView content)
    {
        constexpr static StringLiteral pkg_revision = "Pkg.Revision";

        constexpr static auto is_version_character = [](char ch) {
            return ch == '.' || ParserBase::is_ascii_digit(ch);
        };

        auto first = content.begin();
        auto last = content.end();

        for (;;)
        {
            first = Util::search_and_skip(first, last, pkg_revision);
            if (first == last) break;

            first = std::find_if_not(first, last, ParserBase::is_whitespace);
            if (first == last) break;
            if (*first != '=') continue;

            // Pkg.Revision = x.y.z
            ++first; // skip =
            first = std::find_if_not(first, last, ParserBase::is_whitespace);
            auto end_of_version = std::find_if_not(first, last, is_version_character);
            if (first == end_of_version) continue;
            return StringView{first, end_of_version};
        }

        return {};
    }

    Optional<NdkVersion> to_version(StringView version)
    {
        if (version.size() > 100) return {};
        std::vector<int> fragments;

        for (auto first = version.begin(), last = version.end(); first != last;)
        {
            auto next = std::find(first, last, '.');
            auto parsed = Strings::strto<int>(StringView{first, next});
            if (auto p = parsed.get())
            {
                fragments.push_back(*p);
            }
            else
            {
                return {};
            }
            if (next == last) break;
            ++next;
            first = next;
        }

        if (fragments.size() == 3)
        {
            return NdkVersion{fragments[0], fragments[1], fragments[2]};
        }
        return {};
    }

    static void maven_install(const Path& aar, const Path& pom, const Options& prefab_options)
    {
        if (prefab_options.enable_debug)
        {
            msg::println(Color::warning, msgDeprecatedPrefabDebugOption);
        }
        Debug::print("Installing POM and AAR file to ~/.m2");
        auto cmd = Command{Tools::MAVEN};
        if (!prefab_options.enable_debug)
        {
            cmd.string_arg("-q");
        }

        cmd.string_arg("install:install-file")
            .string_arg(Strings::concat("-Dfile=", aar))
            .string_arg(Strings::concat("-DpomFile=", pom));

        ProcessLaunchSettings settings;
        settings.environment = get_clean_environment();
        const int exit_code = cmd_execute(cmd, settings).value_or_exit(VCPKG_LINE_INFO);

        if (exit_code != 0)
        {
            msg::println_error(msgInstallingMavenFile, msg::path = aar);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }

    static std::unique_ptr<PreBuildInfo> build_info_from_triplet(
        const VcpkgPaths& paths, const std::unique_ptr<CMakeVars::CMakeVarProvider>& provider, const Triplet& triplet)
    {
        provider->load_generic_triplet_vars(triplet);
        return std::make_unique<PreBuildInfo>(
            paths, triplet, provider->get_generic_triplet_vars(triplet).value_or_exit(VCPKG_LINE_INFO));
    }

    static bool is_supported(const PreBuildInfo& info)
    {
        return Strings::case_insensitive_ascii_equals(info.cmake_system_name, "android");
    }

    void do_export(const std::vector<ExportPlanAction>& export_plan,
                   const VcpkgPaths& paths,
                   const Options& prefab_options,
                   const Triplet& default_triplet,
                   const Triplet& host_triplet)
    {
        auto provider = CMakeVars::make_triplet_cmake_var_provider(paths);

        {
            auto build_info = build_info_from_triplet(paths, provider, default_triplet);
            Checks::msg_check_maybe_upgrade(
                VCPKG_LINE_INFO, is_supported(*build_info), msgExportPrefabRequiresAndroidTriplet);
        }

        const auto& triplet_db = paths.get_triplet_db();

        std::unordered_map<CPUArchitecture, std::string> required_archs = {{CPUArchitecture::ARM, "armeabi-v7a"},
                                                                           {CPUArchitecture::ARM64, "arm64-v8a"},
                                                                           {CPUArchitecture::X86, "x86"},
                                                                           {CPUArchitecture::X64, "x86_64"}};

        std::unordered_map<CPUArchitecture, int> cpu_architecture_api_map = {{CPUArchitecture::ARM64, 21},
                                                                             {CPUArchitecture::ARM, 16},
                                                                             {CPUArchitecture::X64, 21},
                                                                             {CPUArchitecture::X86, 16}};

        std::vector<Triplet> triplets;
        std::unordered_map<Triplet, std::string> triplet_abi_map;
        std::unordered_map<Triplet, int> triplet_api_map;

        for (const auto& triplet_file : triplet_db.available_triplets)
        {
            if (!triplet_file.name.empty())
            {
                // The execution of emscripten cmake script causes the prefab export to fail.
                // But here we don't need this execution at all, so we skip it.
                if (triplet_file.name == "wasm32-emscripten") continue;

                Triplet triplet = Triplet::from_canonical_name(triplet_file.name);
                auto triplet_build_info = build_info_from_triplet(paths, provider, triplet);
                if (is_supported(*triplet_build_info))
                {
                    auto cpu_architecture =
                        to_cpu_architecture(triplet_build_info->target_architecture).value_or_exit(VCPKG_LINE_INFO);
                    auto required_arch = required_archs.find(cpu_architecture);
                    if (required_arch != required_archs.end())
                    {
                        triplets.push_back(triplet);
                        triplet_abi_map[triplet] = required_archs[cpu_architecture];
                        triplet_api_map[triplet] = cpu_architecture_api_map[cpu_architecture];
                        required_archs.erase(required_arch);
                    }
                }
            }
        }

        Checks::msg_check_exit(VCPKG_LINE_INFO, required_archs.empty(), msgExportArchitectureReq);

        Optional<std::string> android_ndk_home = get_environment_variable("ANDROID_NDK_HOME");

        if (!android_ndk_home.has_value())
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgMissingAndroidEnv);
        }

        const Filesystem& fs = paths.get_filesystem();

        const Path ndk_location = android_ndk_home.value_or_exit(VCPKG_LINE_INFO);

        Checks::msg_check_maybe_upgrade(VCPKG_LINE_INFO,
                                        fs.exists(ndk_location, IgnoreErrors{}),
                                        msgMissingAndroidHomeDir,
                                        msg::path = ndk_location);
        const auto source_properties_location = ndk_location / "source.properties";

        Checks::msg_check_maybe_upgrade(VCPKG_LINE_INFO,
                                        fs.exists(ndk_location, IgnoreErrors{}),
                                        msgAndroidHomeDirMissingProps,
                                        msg::env_var = format_environment_variable("ANDROID_NDK_HOME"),
                                        msg::path = source_properties_location);

        std::string content = fs.read_contents(source_properties_location, VCPKG_LINE_INFO);

        Optional<std::string> version_opt = find_ndk_version(content);

        Checks::check_maybe_upgrade(VCPKG_LINE_INFO, version_opt.has_value());

        NdkVersion version = to_version(version_opt.value_or_exit(VCPKG_LINE_INFO)).value_or_exit(VCPKG_LINE_INFO);

        fs.remove_all(paths.prefab, VCPKG_LINE_INFO);

        /*
        prefab
        +-- <name>
            +-- aar
            |   +-- AndroidManifest.xml
            |   +-- META-INF
            |   |   +-- LICENCE
            |   +-- prefab
            |       +-- modules
            |       |   +-- <module>
            |       |       +-- include
            |       |       +-- libs
            |       |       |   +-- android.arm64-v8a
            |       |       |   |   +-- abi.json
            |       |       |   |   +-- lib<module>.so
            |       |       |   +-- android.armeabi-v7a
            |       |       |   |   +-- abi.json
            |       |       |   |   +-- lib<module>.so
            |       |       |   +-- android.x86
            |       |       |   |   +-- abi.json
            |       |       |   |   +-- lib<module>.so
            |       |       |   +-- android.x86_64
            |       |       |       +-- abi.json
            |       |       |       +-- lib<module>.so
            |       |       +-- module.json
            |       +-- prefab.json
            +-- <name>-<version>.aar
            +-- pom.xml
        */

        std::unordered_map<std::string, std::string> version_map;

        std::unordered_map<std::string, std::set<PackageSpec>> empty_package_dependencies;

        for (const auto& action : export_plan)
        {
            // cross-compiling
            // Host-only ports (e.g. vcpkg_cmake) are not to be exported.
            if (host_triplet == action.spec.triplet()) continue;

            const std::string name = action.spec.name();
            auto dependencies = action.dependencies();

            const auto action_build_info = read_build_info(fs, paths.build_info_file_path(action.spec));
            const bool is_empty_package = action_build_info.policies.is_enabled(BuildPolicy::EMPTY_PACKAGE);

            if (is_empty_package)
            {
                empty_package_dependencies[name] = std::set<PackageSpec>();
                for (auto&& dependency : dependencies)
                {
                    if (empty_package_dependencies.find(dependency.name()) != empty_package_dependencies.end())
                    {
                        auto& child_deps = empty_package_dependencies[name];
                        auto& parent_deps = empty_package_dependencies[dependency.name()];
                        child_deps.insert(parent_deps.begin(), parent_deps.end());
                    }
                    else
                    {
                        empty_package_dependencies[name].insert(dependency);
                    }
                }
                continue;
            }

            const auto per_package_dir_path = paths.prefab / name;

            const auto& binary_paragraph = action.core_paragraph().value_or_exit(VCPKG_LINE_INFO);

            // The port version is not specified during installation (vcpkg install). Just ignore port version.
            // jsoncpp_1.17#2_x64-android.list -> jsoncpp_1.17_x64-android.list
            const std::string norm_version = binary_paragraph.version.text;

            version_map[name] = norm_version;

            msg::println(msgExportingPackage, msg::package_name = name);

            auto package_directory = per_package_dir_path / "aar";
            auto prefab_directory = package_directory / "prefab";
            auto modules_directory = prefab_directory / "modules";

            fs.create_directories(modules_directory, IgnoreErrors{});

            std::string artifact_id = prefab_options.maybe_artifact_id.value_or(name);
            std::string group_id = prefab_options.maybe_group_id.value_or("com.vcpkg.ndk.support");
            std::string sdk_min_version = prefab_options.maybe_min_sdk.value_or("16");
            std::string sdk_target_version = prefab_options.maybe_target_sdk.value_or("29");

            std::string MANIFEST_TEMPLATE =
                R"(<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="@GROUP_ID@.@ARTIFACT_ID@" android:versionCode="1" android:versionName="1.0">
    <uses-sdk android:minSdkVersion="@MIN_SDK_VERSION@" android:targetSdkVersion="@SDK_TARGET_VERSION@" />
</manifest>)";
            std::string manifest = Strings::replace_all(std::move(MANIFEST_TEMPLATE), "@GROUP_ID@", group_id);
            Strings::inplace_replace_all(manifest, "@ARTIFACT_ID@", artifact_id);
            Strings::inplace_replace_all(manifest, "@MIN_SDK_VERSION@", sdk_min_version);
            Strings::inplace_replace_all(manifest, "@SDK_TARGET_VERSION@", sdk_target_version);

            auto manifest_path = package_directory / "AndroidManifest.xml";
            auto prefab_path = prefab_directory / "prefab.json";

            auto meta_dir = package_directory / "META-INF";

            fs.create_directories(meta_dir, IgnoreErrors{});

            const auto share_root = paths.packages() / fmt::format("{}_{}", name, action.spec.triplet());

            fs.copy_file(share_root / "share" / name / "copyright",
                         meta_dir / "LICENSE",
                         CopyOptions::overwrite_existing,
                         IgnoreErrors{});

            PackageMetadata pm;
            pm.name = artifact_id;
            pm.schema = 1;
            pm.version = norm_version;

            std::set<PackageSpec> dependencies_minus_empty_packages;

            for (auto&& dependency : dependencies)
            {
                if (empty_package_dependencies.find(dependency.name()) != empty_package_dependencies.end())
                {
                    for (auto& empty_package_dep : empty_package_dependencies[dependency.name()])
                    {
                        dependencies_minus_empty_packages.insert(empty_package_dep);
                    }
                }
                else
                {
                    dependencies_minus_empty_packages.insert(dependency);
                }
            }

            std::vector<std::string> pom_dependencies;

            if (!dependencies_minus_empty_packages.empty())
            {
                pom_dependencies.emplace_back("\n<dependencies>");
            }

            for (const auto& it : dependencies_minus_empty_packages)
            {
                std::string maven_pom = R"(    <dependency>
        <groupId>@GROUP_ID@</groupId>
        <artifactId>@ARTIFACT_ID@</artifactId>
        <version>@VERSION@</version>
        <type>aar</type>
        <scope>runtime</scope>
    </dependency>)";
                std::string pom = Strings::replace_all(std::move(maven_pom), "@GROUP_ID@", group_id);
                Strings::inplace_replace_all(pom, "@ARTIFACT_ID@", it.name());
                Strings::inplace_replace_all(pom, "@VERSION@", version_map[it.name()]);
                pom_dependencies.push_back(pom);
                pm.dependencies.push_back(it.name());
            }

            if (!dependencies_minus_empty_packages.empty())
            {
                pom_dependencies.emplace_back("</dependencies>\n");
            }

            if (prefab_options.enable_debug)
            {
                msg::println(Color::warning, msgDeprecatedPrefabDebugOption);
            }

            Debug::print(fmt::format("Writing manifest to \n\t{}", manifest_path));
            Debug::print(fmt::format("Writing prefab meta data to \n\t{}", prefab_path));

            fs.write_contents(manifest_path, manifest, VCPKG_LINE_INFO);
            fs.write_contents(prefab_path, pm.to_json(), VCPKG_LINE_INFO);

            std::vector<std::string> triplet_names;
            triplet_names.reserve(triplets.size());
            for (auto&& triplet : triplets)
            {
                triplet_names.push_back(triplet.canonical_name());
            }

            Debug::print(
                fmt::format("Found {} triplets:\n\t{}\n", triplets.size(), Strings::join("\n\t", triplet_names)));

            for (const auto& triplet : triplets)
            {
                const auto listfile =
                    paths.installed().vcpkg_dir_info() / fmt::format("{}_{}_{}.list", name, norm_version, triplet);
                const auto installed_dir = paths.packages() / fmt::format("{}_{}", name, triplet);
                if (!(fs.exists(listfile, IgnoreErrors{})))
                {
                    msg::println_error(msgCorruptedInstallTree);
                    msg::println_error(msgFileNotFound, msg::path = listfile);

                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                auto libs = installed_dir / "lib";

                std::vector<Path> modules;

                std::vector<Path> modules_shared = find_modules(paths, libs, ".so");

                modules.reserve(modules_shared.size());
                for (const auto& module : modules_shared)
                {
                    modules.push_back(module);
                }

                std::vector<Path> modules_static = find_modules(paths, libs, ".a");
                for (const auto& module : modules_static)
                {
                    modules.push_back(module);
                }

                // header only libs
                if (modules.empty())
                {
                    auto module_dir = modules_directory / name;
                    auto module_libs_dir = module_dir / "libs";
                    fs.create_directories(module_libs_dir, IgnoreErrors{});
                    auto installed_headers_dir = installed_dir / "include";
                    auto exported_headers_dir = module_dir / "include";

                    ModuleMetadata meta;
                    auto module_meta_path = module_dir / "module.json";
                    fs.write_contents(module_meta_path, meta.to_json(), VCPKG_LINE_INFO);

                    fs.copy_regular_recursive(installed_headers_dir, exported_headers_dir, VCPKG_LINE_INFO);
                    break;
                }
                else
                {
                    for (const auto& module : modules)
                    {
                        auto module_name = module.stem().to_string();
                        auto extension = module.extension();

                        ABIMetadata ab;
                        ab.abi = triplet_abi_map[triplet];
                        ab.api = triplet_api_map[triplet];

                        ab.stl = Strings::contains(extension, 'a') ? "c++_static" : "c++_shared";
                        ab.ndk = version.major();

                        Debug::print(fmt::format("Found module {} {}", module_name, ab.abi));
                        Strings::inplace_trim(module_name);

                        if (Strings::starts_with(module_name, "lib"))
                        {
                            module_name = module_name.substr(3);
                        }
                        auto module_dir = modules_directory / module_name;
                        auto module_libs_dir = module_dir / "libs" / fmt::format("android.{}", ab.abi);
                        fs.create_directories(module_libs_dir, IgnoreErrors{});

                        auto abi_path = module_libs_dir / "abi.json";

                        Debug::print(fmt::format("Writing abi metadata to {}", abi_path));
                        fs.write_contents(abi_path, ab.to_string(), VCPKG_LINE_INFO);

                        auto installed_module_path = libs / module.filename();
                        auto exported_module_path = module_libs_dir / module.filename();

                        fs.copy_file(installed_module_path,
                                     exported_module_path,
                                     CopyOptions::overwrite_existing,
                                     IgnoreErrors{});

                        Debug::print(
                            "Copying libraries...\nFrom {}\nTo {}", installed_module_path, exported_module_path);
                        auto installed_headers_dir = installed_dir / "include";
                        auto exported_headers_dir = module_libs_dir / "include";

                        Debug::print("Copying headers...\nFrom {}\nTo {}", installed_headers_dir, exported_headers_dir);
                        fs.copy_regular_recursive(installed_headers_dir, exported_headers_dir, VCPKG_LINE_INFO);

                        ModuleMetadata meta;
                        auto module_meta_path = module_dir / "module.json";

                        Debug::print(fmt::format("Writing module metadata to {}", module_meta_path));
                        fs.write_contents(module_meta_path, meta.to_json(), VCPKG_LINE_INFO);
                    }
                }
            }

            auto exported_archive_path = per_package_dir_path / fmt::format("{}-{}.aar", name, norm_version);
            auto pom_path = per_package_dir_path / "pom.xml";

            Debug::print(
                fmt::format("Exporting AAR and POM\n\tAAR path {}\n\tPOM path {}", exported_archive_path, pom_path));

            auto zip = ZipTool::make(paths.get_tool_cache(), out_sink).value_or_exit(VCPKG_LINE_INFO);

            auto compress_result =
                zip.compress_directory_to_zip(paths.get_filesystem(), package_directory, exported_archive_path);
            if (!compress_result)
            {
                Checks::msg_exit_with_message(
                    VCPKG_LINE_INFO,
                    std::move(compress_result)
                        .error()
                        .append(msgCompressFolderFailed, msg::path = package_directory.native()));
            }

            std::string POM = R"(<?xml version="1.0" encoding="UTF-8"?>
<project xmlns="http://maven.apache.org/POM/4.0.0"
         xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
         xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 http://maven.apache.org/xsd/maven-4.0.0.xsd">
    <modelVersion>4.0.0</modelVersion>


    <groupId>@GROUP_ID@</groupId>
    <artifactId>@ARTIFACT_ID@</artifactId>
    <version>@VERSION@</version>
    <packaging>aar</packaging>
    <description>The Vcpkg AAR for @ARTIFACT_ID@</description>
    <url>https://github.com/microsoft/vcpkg.git</url>
    @DEPENDENCIES@
</project>)";

            std::string pom = Strings::replace_all(std::move(POM), "@GROUP_ID@", group_id);
            Strings::inplace_replace_all(pom, "@ARTIFACT_ID@", artifact_id);
            Strings::inplace_replace_all(pom, "@DEPENDENCIES@", Strings::join("\n", pom_dependencies));
            Strings::inplace_replace_all(pom, "@VERSION@", norm_version);

            fs.write_contents(pom_path, pom, VCPKG_LINE_INFO);

            if (prefab_options.enable_maven)
            {
                maven_install(exported_archive_path, pom_path, prefab_options);

                Debug::println(
                    fmt::format("Configuration properties in Android Studio\nIn app/build.gradle\n\n\t{}:{}:{}",
                                group_id,
                                artifact_id,
                                norm_version));
                msg::write_unlocalized_text(Color::none, R"(And cmake flags

    externalNativeBuild {
                cmake {
                    arguments '-DANDROID_STL=c++_shared'
                    cppFlags "-std=c++17"
                }
            }

)");

                msg::write_unlocalized_text(Color::none, R"(In gradle.properties

    android.enablePrefab=true
    android.enableParallelJsonGen=false
    android.prefabVersion=${prefab.version}

)");
            }
            msg::println(Color::success, msgSuccessfulyExported, msg::package_name = name, msg::path = paths.prefab);
        }
    }
}
