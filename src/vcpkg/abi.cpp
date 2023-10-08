#include <vcpkg/base/cache.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
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

namespace vcpkg
{
    // This struct caches precomputable abis
    struct AbiCache
    {
        std::vector<AbiEntry> cmake_script_hashes;
    };

    static std::string grdk_hash(const Filesystem& fs,
                                 Cache<Path, Optional<std::string>>& grdk_cache,
                                 const PreBuildInfo& pre_build_info)
    {
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
                                                Cache<Path, Optional<std::string>>& grdk_cache,
                                                const PreBuildInfo& pre_build_info,
                                                std::vector<AbiEntry>& abi_tag_entries)
    {
        if (pre_build_info.public_abi_override)
        {
            abi_tag_entries.emplace_back(
                "public_abi_override",
                Hash::get_string_hash(pre_build_info.public_abi_override.value_or_exit(VCPKG_LINE_INFO),
                                      Hash::Algorithm::Sha256));
        }

        for (const auto& env_var : pre_build_info.passthrough_env_vars_tracked)
        {
            auto maybe_env_var_value = get_environment_variable(env_var);
            if (auto env_var_value = maybe_env_var_value.get())
            {
                abi_tag_entries.emplace_back("ENV:" + env_var,
                                             Hash::get_string_hash(*env_var_value, Hash::Algorithm::Sha256));
            }
        }

        if (pre_build_info.target_is_xbox)
        {
            abi_tag_entries.emplace_back("grdk.h", grdk_hash(fs, grdk_cache, pre_build_info));
        }
    }

    static std::vector<AbiEntry> get_common_abi(const VcpkgPaths& paths)
    {
        std::vector<AbiEntry> abi_entries;
        auto& fs = paths.get_filesystem();

        abi_entries.emplace_back("cmake", paths.get_tool_version(Tools::CMAKE, stdout_sink));
        abi_entries.emplace_back("ports.cmake", Hash::get_file_hash(fs, paths.ports_cmake, Hash::Algorithm::Sha256));
        abi_entries.emplace_back("post_build_checks", "2");

        // This #ifdef is mirrored in tools.cpp's PowershellProvider
#if defined(_WIN32)
        abi_entries.emplace_back("powershell", paths.get_tool_version("powershell-core", stdout_sink));
#endif
    }

    static std::vector<AbiEntry> get_cmake_script_hashes(const Filesystem& fs, const Path& scripts_dir)
    {
        auto files = fs.get_regular_files_non_recursive(scripts_dir / "cmake", VCPKG_LINE_INFO);

        std::vector<AbiEntry> helpers;
        helpers.reserve(files.size());

        for (auto&& file : files)
        {
            if (file.filename() == ".DS_Store")
            {
                continue;
            }
            helpers.emplace_back(file.stem().to_string(),
                                 Hash::get_file_hash(fs, file, Hash::Algorithm::Sha256).value_or_exit(VCPKG_LINE_INFO));
        }
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
    static std::pair<std::vector<std::string>, std::string> get_port_files(const Filesystem& fs,
                                                                           std::vector<AbiEntry> abi_entries,
                                                                           const SourceControlFileAndLocation& scfl)
    {
        auto files_and_content = get_ports_files_and_contents(fs, scfl.source_location);
        auto& paragraph = scfl.source_control_file->core_paragraph;

        // If there is an unusually large number of files in the port then
        // something suspicious is going on.
        constexpr size_t max_port_file_count = 100;

        if (files_and_content.size() > max_port_file_count)
        {
            msg::println_warning(
                msgHashPortManyFiles, msg::package_name = paragraph->name, msg::count = files_and_content.size());
        }

        std::vector<std::string> port_files;
        abi_entries.reserve(files_and_content.size());

        std::string portfile_cmake_contents;
        for (auto&& [port_file, contents] : files_and_content)
        {
            if (port_file.extension() == ".cmake")
            {
                portfile_cmake_contents += contents;
            }

            auto path_str = std::move(port_file).native();
            port_files.emplace_back(path_str);
            abi_entries.emplace_back(std::move(path_str), Hash::get_string_sha256(contents));
        }
        return {std::move(port_files), std::move(portfile_cmake_contents)};
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

    // calculate abi of port-internal files and system environment
    static Optional<std::vector<AbiEntry>> make_private_abi(const VcpkgPaths& paths,
                                                            InstallPlanAction& action,
                                                            std::unique_ptr<PreBuildInfo>&& proto_pre_build_info,
                                                            Cache<Path, Optional<std::string>>& grdk_cache)
    {
        const auto& pre_build_info = *proto_pre_build_info;
        // Cant't be called in parallel
        const auto& toolset = paths.get_toolset(pre_build_info);
        auto& abi_info = action.abi_info.emplace();
        abi_info.pre_build_info = std::move(proto_pre_build_info);
        abi_info.toolset.emplace(toolset);

        if (Util::Enum::to_bool(action.build_options.only_downloads)) return nullopt;

        if (action.build_options.use_head_version == UseHeadVersion::YES)
        {
            Debug::print("Binary caching for package ", action.spec, " is disabled due to --head\n");
            return nullopt;
        }
        if (action.build_options.editable == Editable::YES)
        {
            Debug::print("Binary caching for package ", action.spec, " is disabled due to --editable\n");
            return nullopt;
        }

        auto& fs = paths.get_filesystem();
        std::vector<AbiEntry> abi_tag_entries;
        abi_entries_from_pre_build_info(fs, grdk_cache, *action.abi_info.get()->pre_build_info, abi_tag_entries);

        auto& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
        auto&& [port_files, cmake_contents] = get_port_files(fs, abi_tag_entries, scfl);

        for (auto&& helper : paths.get_cmake_script_hashes())
        {
            if (Strings::case_insensitive_ascii_contains(cmake_contents, helper.first))
            {
                abi_tag_entries.emplace_back(helper.first, helper.second);
            }
        }
        abi_info.heuristic_resources.push_back(
            run_resource_heuristics(cmake_contents, scfl.source_control_file->core_paragraph->raw_version));

        get_feature_abi(action.feature_list, abi_tag_entries);

        return abi_tag_entries;
    }

    void populate_abi_tag_ex()
    {
        // get toolset (not in parallel)
        // get prebuildinfo
        // return when using editable or head flags

        // get compiler info (not in parallel)

        // check if all dependencies have a hash if not, return (not in parallel) (sequencial)

        // get triplet info (not in parallel)

        // abi tags from prebuildinfo

        // portfile hashes/contents
        // cmake helpers (access to cmake helper hashes in cache not in parallel)

        // cmake/ PS version (precompute, same for all)
        // ports.cmake version (precompute, same for all)

        // ports.cmake
        // post build lint (not updated in 3 years! still needed?)

        // port features

        // sort
        // debug output

        // check for missing abi tags

        // fillout port abi
        // fillout infos for sbom
        // write abi file

        // write sbom file?
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
                    Checks::unreachable(
                        VCPKG_LINE_INFO,
                        fmt::format("Failed to find dependency abi for {} -> {}", current_action->spec, dependency_spec));
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

    void compute_all_abis_ex(const VcpkgPaths& paths,
                             ActionPlan& action_plan,
                             const CMakeVars::CMakeVarProvider& var_provider,
                             const StatusParagraphs& status_db)
    {
        const auto cmake_script_hashes = get_cmake_script_hashes(paths.get_filesystem(), paths.scripts);
        // 1. system abi (ports.cmake/ PS version/ CMake version)
        const auto common_abi = get_common_abi(paths);

        for (auto it = action_plan.install_actions.begin(); it != action_plan.install_actions.end(); ++it)
        {
            auto& action = *it;
            std::vector<AbiEntry> dependency_abis;

            if (!Util::Enum::to_bool(action.build_options.only_downloads))
            {
                dependency_abis = get_dependency_abis(action_plan.install_actions.begin(), it, status_db);
            }
        }

        // get prebuildinfo (not in parallel)
        // populate abi tag
    }

    void compute_all_abis(const VcpkgPaths& paths,
                          ActionPlan& action_plan,
                          const CMakeVars::CMakeVarProvider& var_provider,
                          const StatusParagraphs& status_db)
    {
        Cache<Path, Optional<std::string>> grdk_cache;

        std::vector<std::reference_wrapper<InstallPlanAction>> must_make_private_abi;

        for (auto& action : action_plan.install_actions)
        {
            if (action.abi_info.has_value()) continue;

            must_make_private_abi.push_back(action);
        }

        std::vector<Optional<std::vector<AbiEntry>>> action_private_abis(must_make_private_abi.size());

        // get "private" abi
        parallel_transform(must_make_private_abi.begin(),
                           must_make_private_abi.size(),
                           action_private_abis.begin(),
                           [&](auto&& action) {
                               return populate_abi_tag(paths, action, grdk_cache);
                               make_private_abi(
                                   paths,
                                   action,
                                   std::make_unique<PreBuildInfo>(
                                       paths,
                                       action.spec.triplet(),
                                       var_provider.get_tag_vars(action.spec).value_or_exit(VCPKG_LINE_INFO)),
                                   grdk_cache);
                           });

        for (auto&& maybe_abi_entry : action_private_abis)
        {
            if (!maybe_abi_entry.has_value())
            {
                continue;
            }
        }

        // get dependencies abi
        for (auto it = action_plan.install_actions.begin(); it != action_plan.install_actions.end(); ++it)
        {
            auto& action = *it;
            std::vector<AbiEntry> dependency_abis;
            for (auto&& pspec : action.package_dependencies)
            {
                if (pspec == action.spec) continue;

                auto pred = [&](const InstallPlanAction& ipa) { return ipa.spec == pspec; };
                auto it2 = std::find_if(action_plan.install_actions.begin(), it, pred);
                if (it2 == it)
                {
                    // Finally, look in current installed
                    auto status_it = status_db.find(pspec);
                    if (status_it == status_db.end())
                    {
                        Checks::unreachable(
                            VCPKG_LINE_INFO,
                            fmt::format("Failed to find dependency abi for {} -> {}", action.spec, pspec));
                    }

                    dependency_abis.emplace_back(pspec.name(), status_it->get()->package.abi);
                }
                else
                {
                    dependency_abis.emplace_back(pspec.name(), it2->public_abi());
                }
            }

#ifdef ABI_PERF
            auto maybe_abi_path_and_file =
#endif
                populate_abi_tag(paths, action, dependency_abis, grdk_cache);
#ifdef ABI_PERF
            if (auto abi_path_and_file = maybe_abi_path_and_file.get())
            {
                abi_files_and_contents.emplace_back(std::move(*abi_path_and_file));
            }
#endif
        }
#ifdef ABI_PERF
        std::mutex mtx;
        auto& fs = paths.get_filesystem();
        bool should_exit = false;
        parallel_for_each_n(abi_files_and_contents.begin(), abi_files_and_contents.size(), [&](const auto& abi_entry) {
            std::error_code ec;

            fs.write_contents_and_dirs(abi_entry.first, abi_entry.second, ec);
            if (ec)
            {
                std::lock_guard lock(mtx);
                msg::println_error(format_filesystem_call_error(ec, "create_directory", {abi_entry.first}));
                should_exit = true;
            }
        });
        if (should_exit)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
#endif
    }
} // namespace vcpkg
