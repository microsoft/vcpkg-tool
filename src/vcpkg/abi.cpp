#include <vcpkg/base/cache.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/lazy.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parallel-algorithms.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <vcpkg/abi.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/spdx.h>
#include <vcpkg/tools.h>

#include <array>
#include <mutex>
#include <utility>
#include <vector>

namespace vcpkg
{
    // This struct caches precomputable abis
    struct AbiCache
    {
        std::vector<AbiEntry> cmake_script_hashes;
    };

    struct PackageAbiResult
    {
        const InstallPlanAction& action;
        std::string abi_str;
        std::string cmake_content;
        std::vector<AbiEntry> port_files_abi;
    };

    static std::string grdk_hash(const Filesystem& fs, const PreBuildInfo& pre_build_info)
    {
        static const Cache<Path, Optional<std::string>> grdk_cache;
        if (auto game_dk_latest = pre_build_info.gamedk_latest_path.get())
        {
            const auto grdk_header_path = *game_dk_latest / "GRDK/gameKit/Include/grdk.h";
            const auto& maybe_header_hash = grdk_cache.get_lazy(grdk_header_path, [&]() -> Optional<std::string> {
                auto maybe_hash = Hash::get_file_hash(fs, grdk_header_path, Hash::Algorithm::Sha256);
                if (auto hash = maybe_hash.get())
                {
                    return std::move(*hash);
                }
                else
                {
                    return nullopt;
                }
            });

            if (auto header_hash = maybe_header_hash.get())
            {
                return *header_hash;
            }
        }

        return "none";
    }

    static void abi_entries_from_pre_build_info(const Filesystem& fs,
                                                const PreBuildInfo& pre_build_info,
                                                std::vector<AbiEntry>& abi_tag_entries)
    {
        if (pre_build_info.public_abi_override)
        {
            abi_tag_entries.emplace_back(
                "public_abi_override",
                Hash::get_string_sha256(pre_build_info.public_abi_override.value_or_exit(VCPKG_LINE_INFO)));
        }

        for (const auto& env_var : pre_build_info.passthrough_env_vars_tracked)
        {
            auto maybe_env_var_value = get_environment_variable(env_var);
            if (auto env_var_value = maybe_env_var_value.get())
            {
                abi_tag_entries.emplace_back("ENV:" + env_var, Hash::get_string_sha256(*env_var_value));
            }
        }

        if (pre_build_info.target_is_xbox)
        {
            abi_tag_entries.emplace_back("grdk.h", grdk_hash(fs, pre_build_info));
        }
    }

    static std::vector<AbiEntry> get_common_abi(const VcpkgPaths& paths)
    {
        std::vector<AbiEntry> abi_entries;
        auto& fs = paths.get_filesystem();

        abi_entries.emplace_back("cmake", paths.get_tool_version(Tools::CMAKE, stdout_sink));
        abi_entries.emplace_back("ports.cmake",
                                 Hash::get_file_hash(fs, paths.ports_cmake, Hash::Algorithm::Sha256).value_or(""));
        abi_entries.emplace_back("post_build_checks", "2");

        // This #ifdef is mirrored in tools.cpp's PowershellProvider
#if defined(_WIN32)
        abi_entries.emplace_back("powershell", paths.get_tool_version("powershell-core", stdout_sink));
#endif
        return abi_entries;
    }

    static std::vector<AbiEntry> get_cmake_script_hashes(const Filesystem& fs, const Path& scripts_dir)
    {
        auto files = fs.get_regular_files_non_recursive(scripts_dir / "cmake", VCPKG_LINE_INFO);
        Util::erase_remove_if(files, [](const Path& file) { return file.filename() == ".DS_Store"; });

        std::vector<AbiEntry> helpers(files.size());

        parallel_transform(files.begin(), files.size(), helpers.begin(), [&fs](auto&& file) {
            return AbiEntry{file.stem().to_string(),
                            Hash::get_file_hash(fs, file, Hash::Algorithm::Sha256).value_or("")};
        });
        return helpers;
    }

    static std::vector<std::pair<Path, std::string>> get_ports_files_and_contents(const Filesystem& fs,
                                                                                  const Path& port_root_dir)
    {
        auto files = fs.get_regular_files_recursive_lexically_proximate(port_root_dir, VCPKG_LINE_INFO);

        std::vector<std::pair<Path, std::string>> files_and_content;
        files_and_content.reserve(files.size());

        for (auto&& file : files)
        {
            if (file.filename() == ".DS_Store")
            {
                continue;
            }
            std::string contents = fs.read_contents(port_root_dir / file, VCPKG_LINE_INFO);
            files_and_content.emplace_back(std::move(file), std::move(contents));
        }
        return files_and_content;
    }

    // Get abi for per-port files
    static std::pair<std::vector<AbiEntry>, std::string> get_port_files(const Filesystem& fs,
                                                                        const SourceControlFileAndLocation& scfl)
    {
        auto files_and_content = get_ports_files_and_contents(fs, scfl.source_location);

        // If there is an unusually large number of files in the port then
        // something suspicious is going on.
        static constexpr size_t max_port_file_count = 100;

        if (files_and_content.size() > max_port_file_count)
        {
            auto& paragraph = scfl.source_control_file->core_paragraph;
            msg::println_warning(
                msgHashPortManyFiles, msg::package_name = paragraph->name, msg::count = files_and_content.size());
        }

        std::vector<AbiEntry> abi_entries;
        abi_entries.reserve(files_and_content.size());

        std::string portfile_cmake_contents;
        for (auto&& [port_file, contents] : files_and_content)
        {
            if (port_file.extension() == ".cmake")
            {
                portfile_cmake_contents += contents;
            }

            std::string path_str = std::move(port_file).generic_u8string();
            abi_entries.emplace_back(std::move(path_str), Hash::get_string_sha256(contents));
        }
        return {std::move(abi_entries), std::move(portfile_cmake_contents)};
    }

    static void get_feature_abi(InternalFeatureSet sorted_feature_list, std::vector<AbiEntry>& abi_entries)
    {
        // Check that no "default" feature is present. Default features must be resolved before attempting to calculate
        // a package ABI, so the "default" should not have made it here.
        static constexpr StringLiteral default_literal{"default"};
        const bool has_no_pseudo_features = std::none_of(
            sorted_feature_list.begin(), sorted_feature_list.end(), [](StringView s) { return s == default_literal; });
        Checks::check_exit(VCPKG_LINE_INFO, has_no_pseudo_features);
        Util::sort_unique_erase(sorted_feature_list);

        // Check that the "core" feature is present. After resolution into InternalFeatureSet "core" meaning "not
        // default" should have already been handled so "core" should be here.
        Checks::check_exit(
            VCPKG_LINE_INFO,
            std::binary_search(sorted_feature_list.begin(), sorted_feature_list.end(), StringLiteral{"core"}));

        abi_entries.emplace_back("features", Strings::join(";", sorted_feature_list));
    }

    // PRE: Check if debugging is enabled
    static void print_debug_info(const PackageSpec& spec, View<AbiEntry> abi_entries)
    {
        std::string message = Strings::concat("[DEBUG] <abientries for ", spec, ">\n");
        for (auto&& entry : abi_entries)
        {
            Strings::append(message, "[DEBUG]   ", entry.key, "|", entry.value, "\n");
        }
        Strings::append(message, "[DEBUG] </abientries>\n");
        msg::write_unlocalized_text_to_stdout(Color::none, message);
    }

    static void print_missing_abi_tags(View<AbiEntry> abi_entries)
    {
        bool printed_first_line = false;
        for (const auto& abi_tag : abi_entries)
        {
            if (!abi_tag.value.empty())
            {
                continue;
            }
            if (!printed_first_line)
            {
                Debug::print("Warning: abi keys are missing values:\n");
                printed_first_line = true;
            }
            Debug::println(abi_tag.key);
        }
    }

    // PRE: !action.abi_info.has_value()
    static bool initialize_abi_info(const VcpkgPaths& paths,
                                    InstallPlanAction& action,
                                    std::unique_ptr<PreBuildInfo>&& proto_pre_build_info)
    {
        const auto& pre_build_info = *proto_pre_build_info;
        // get toolset (not in parallel)
        const auto& toolset = paths.get_toolset(pre_build_info);
        auto& abi_info = action.abi_info.emplace();
        abi_info.pre_build_info = std::move(proto_pre_build_info);

        // return when using editable or head flags
        if (action.build_options.use_head_version == UseHeadVersion::YES)
        {
            Debug::print("Binary caching for package ", action.spec, " is disabled due to --head\n");
            return false;
        }
        if (action.build_options.editable == Editable::YES)
        {
            Debug::print("Binary caching for package ", action.spec, " is disabled due to --editable\n");
            return false;
        }

        // get compiler info (not in parallel)
        abi_info.compiler_info = paths.get_compiler_info(pre_build_info, toolset);
        // get triplet info (not in parallel)
        abi_info.triplet_abi.emplace(paths.get_triplet_info(pre_build_info, toolset));

        return true;
    }

    // check if all dependencies have a hash
    static bool check_dependency_hashes(View<AbiEntry> dependency_abis, const PackageSpec& spec)
    {
        if (!dependency_abis.empty())
        {
            auto dep_no_hash_it = std::find_if(dependency_abis.begin(), dependency_abis.end(), [](const auto& dep_abi) {
                return dep_abi.value.empty();
            });
            if (dep_no_hash_it != dependency_abis.end())
            {
                Debug::print("Binary caching for package ",
                             spec,
                             " is disabled due to missing abi info for ",
                             dep_no_hash_it->key,
                             '\n');
                return false;
            }
        }
        return true;
    }

    // PRE: initialize_abi_tag() was called and returned true
    static PackageAbiResult make_abi_tag(const VcpkgPaths& paths,
                                         InstallPlanAction& action,
                                         View<AbiEntry> common_abi,
                                         std::vector<AbiEntry>&& dependency_abis,
                                         const std::vector<AbiEntry>& cmake_script_hashes)
    {
        auto& fs = paths.get_filesystem();
        AbiInfo& abi_info = *action.abi_info.get();
        auto abi_tag_entries = std::move(dependency_abis);

        abi_tag_entries.emplace_back("triplet", action.spec.triplet().canonical_name());
        abi_tag_entries.emplace_back("triplet_abi", *abi_info.triplet_abi.get());

        // abi tags from prebuildinfo
        abi_entries_from_pre_build_info(fs, *abi_info.pre_build_info, abi_tag_entries);

        auto& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);

        // portfile hashes/contents
        auto&& [port_files_abi, cmake_contents] = get_port_files(fs, scfl);
        abi_tag_entries.insert(abi_tag_entries.end(), port_files_abi.cbegin(), port_files_abi.cend());

        // cmake helpers (access to cmake helper hashes in cache not in parallel)
        for (auto&& helper : cmake_script_hashes)
        {
            if (Strings::case_insensitive_ascii_contains(cmake_contents, helper.key))
            {
                abi_tag_entries.emplace_back(helper.key, helper.value);
            }
        }

        // cmake/ PS version (precompute, same for all)
        // ports.cmake version (precompute, same for all)
        // ports.cmake (precompute, same for all)
        // post build lint (not updated in 3 years! still needed?)
        abi_tag_entries.insert(abi_tag_entries.end(), common_abi.begin(), common_abi.end());

        // port features
        get_feature_abi(action.feature_list, abi_tag_entries);

        // sort
        Util::sort(abi_tag_entries);

        if (Debug::g_debugging)
        {
            // debug output
            print_debug_info(action.spec, abi_tag_entries);

            // check for missing abi tags
            print_missing_abi_tags(abi_tag_entries);
        }

        // fill out port abi
        std::string full_abi_info =
            Strings::join("", abi_tag_entries, [](const AbiEntry& p) { return p.key + " " + p.value + "\n"; });
        abi_info.package_abi = Hash::get_string_sha256(full_abi_info);
        abi_info.abi_tag_file.emplace(paths.build_dir(action.spec) / action.spec.triplet().canonical_name() +
                                      ".vcpkg_abi_info.txt");

        return {action, std::move(full_abi_info), std::move(cmake_contents), std::move(port_files_abi)};
    }

    static std::vector<AbiEntry> get_dependency_abis(std::vector<vcpkg::InstallPlanAction>::iterator action_plan_begin,
                                                     std::vector<vcpkg::InstallPlanAction>::iterator current_action,
                                                     const StatusParagraphs& status_db)
    {
        std::vector<AbiEntry> dependency_abis;
        dependency_abis.reserve(current_action->package_dependencies.size());

        for (const auto& dependency_spec : current_action->package_dependencies)
        {
            // depends on itself?
            if (dependency_spec == current_action->spec)
            {
                continue;
            }

            // find dependency in downstream ports
            auto dependency_it =
                std::find_if(action_plan_begin, current_action, [&dependency_spec](const auto& action) {
                    return action.spec == dependency_spec;
                });

            if (dependency_it == current_action)
            {
                // dependency not found
                // dependency already installed and therefore not in action plan?
                auto status_it = status_db.find(dependency_spec);
                if (status_it == status_db.end())
                {
                    // also not installed --> can't be true
                    Checks::unreachable(VCPKG_LINE_INFO,
                                        fmt::format("Failed to find dependency abi for {} -> {}",
                                                    current_action->spec,
                                                    dependency_spec));
                }

                dependency_abis.emplace_back(dependency_spec.name(), status_it->get()->package.abi);
            }
            else
            {
                // dependency found in action plan
                dependency_abis.emplace_back(dependency_spec.name(), dependency_it->public_abi());
            }
        }
        return dependency_abis;
    }

    void compute_all_abis(const VcpkgPaths& paths,
                          ActionPlan& action_plan,
                          const CMakeVars::CMakeVarProvider& var_provider,
                          const StatusParagraphs& status_db)
    {
        auto& fs = paths.get_filesystem();
        static const std::vector<AbiEntry> cmake_script_hashes = get_cmake_script_hashes(fs, paths.scripts);
        
        // 1. system abi (ports.cmake/ PS version/ CMake version)
        static const auto common_abi = get_common_abi(paths);

        std::vector<PackageAbiResult> abi_results;
        abi_results.reserve(action_plan.install_actions.size());

        for (auto it = action_plan.install_actions.begin(); it != action_plan.install_actions.end(); ++it)
        {
            auto& action = *it;

            Checks::check_exit(VCPKG_LINE_INFO, !action.abi_info.has_value());

            // get prebuildinfo (not in parallel)
            auto pre_build_info = std::make_unique<PreBuildInfo>(
                paths, action.spec.triplet(), var_provider.get_tag_vars(action.spec).value_or_exit(VCPKG_LINE_INFO));

            bool should_proceed = initialize_abi_info(paths, action, std::move(pre_build_info));

            if (!should_proceed)
            {
                continue;
            }

            std::vector<AbiEntry> dependency_abis;
            if (!Util::Enum::to_bool(action.build_options.only_downloads))
            {
                dependency_abis = get_dependency_abis(action_plan.install_actions.begin(), it, status_db);
                if (!check_dependency_hashes(dependency_abis, action.spec))
                {
                    continue;
                }
            }

            abi_results.push_back(
                make_abi_tag(paths, action, common_abi, std::move(dependency_abis), cmake_script_hashes));
        }
        // populate abi tag
        for (auto&& abi_result : abi_results)
        {
            // write abi tag file
            fs.write_contents_and_dirs(
                abi_result.action.abi_info.value_or_exit(VCPKG_LINE_INFO).abi_tag_file.value_or_exit(VCPKG_LINE_INFO),
                abi_result.abi_str,
                VCPKG_LINE_INFO);

            // make and write sbom file
            auto& scf =
                abi_result.action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).source_control_file;
            std::vector<Json::Value> heuristic_resources{
                run_resource_heuristics(abi_result.cmake_content, scf->core_paragraph->raw_version)};
            write_sbom(paths, abi_result.action, std::move(heuristic_resources), abi_result.port_files_abi);
        }
    }
} // namespace vcpkg
