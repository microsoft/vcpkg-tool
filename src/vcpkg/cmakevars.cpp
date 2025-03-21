#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/buildenvironment.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;
namespace vcpkg::CMakeVars
{

    void CMakeVarProvider::load_tag_vars(const ActionPlan& action_plan, Triplet host_triplet) const
    {
        std::vector<FullPackageSpec> install_package_specs;
        std::vector<Path> port_locations;
        install_package_specs.reserve(action_plan.install_actions.size());
        port_locations.reserve(action_plan.install_actions.size());
        for (auto&& action : action_plan.install_actions)
        {
            install_package_specs.emplace_back(action.spec, action.feature_list);
            port_locations.emplace_back(
                action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).port_directory());
        }

        load_tag_vars(install_package_specs, port_locations, host_triplet);
    }

    const std::unordered_map<std::string, std::string>& CMakeVarProvider::get_or_load_dep_info_vars(
        const PackageSpec& spec, Triplet host_triplet) const
    {
        auto maybe_vars = get_dep_info_vars(spec);
        if (!maybe_vars.has_value())
        {
            load_dep_info_vars({&spec, 1}, host_triplet);
            maybe_vars = get_dep_info_vars(spec);
        }
        return maybe_vars.value_or_exit(VCPKG_LINE_INFO);
    }

    namespace
    {
        struct TripletCMakeVarProvider : CMakeVarProvider
        {
            explicit TripletCMakeVarProvider(const vcpkg::VcpkgPaths& paths) : paths(paths) { }
            TripletCMakeVarProvider(const TripletCMakeVarProvider&) = delete;
            TripletCMakeVarProvider& operator=(const TripletCMakeVarProvider&) = delete;

            void load_generic_triplet_vars(Triplet triplet) const override;

            void load_dep_info_vars(View<PackageSpec> specs, Triplet host_triplet) const override;

            void load_tag_vars(View<FullPackageSpec> specs,
                               View<Path> port_locations,
                               Triplet host_triplet) const override;

            Optional<const std::unordered_map<std::string, std::string>&> get_generic_triplet_vars(
                Triplet triplet) const override;

            Optional<const std::unordered_map<std::string, std::string>&> get_dep_info_vars(
                const PackageSpec& spec) const override;

            Optional<const std::unordered_map<std::string, std::string>&> get_tag_vars(
                const PackageSpec& spec) const override;

        public:
            Path create_tag_extraction_file(
                const View<std::pair<FullPackageSpec, std::string>> spec_abi_settings) const;

            Path create_dep_info_extraction_file(const View<PackageSpec> specs) const;

            void launch_and_split(const Path& script_path,
                                  std::vector<std::vector<std::pair<std::string, std::string>>>& vars) const;

            const VcpkgPaths& paths;
            mutable std::unordered_map<PackageSpec, std::unordered_map<std::string, std::string>> dep_resolution_vars;
            mutable std::unordered_map<PackageSpec, std::unordered_map<std::string, std::string>> tag_vars;
            mutable std::unordered_map<Triplet, std::unordered_map<std::string, std::string>> generic_triplet_vars;
        };
    }

    std::unique_ptr<CMakeVarProvider> make_triplet_cmake_var_provider(const vcpkg::VcpkgPaths& paths)
    {
        return std::make_unique<TripletCMakeVarProvider>(paths);
    }

    static std::string create_extraction_file_prelude(const VcpkgPaths& paths,
                                                      const std::map<Triplet, int>& emitted_triplets)
    {
        const auto& fs = paths.get_filesystem();
        std::string extraction_file;

        extraction_file.append("cmake_minimum_required(VERSION 3.5)\n"
                               "macro(vcpkg_triplet_file VCPKG_TRIPLET_ID)\n"
                               "set(_vcpkg_triplet_file_BACKUP_CURRENT_LIST_FILE \"${CMAKE_CURRENT_LIST_FILE}\")\n");

        for (auto&& p : emitted_triplets)
        {
            auto path_to_triplet = paths.get_triplet_db().get_triplet_file_path(p.first);
            fmt::format_to(std::back_inserter(extraction_file),
                           "if(VCPKG_TRIPLET_ID EQUAL {})\n"
                           "set(CMAKE_CURRENT_LIST_FILE \"{}\")\n"
                           "get_filename_component(CMAKE_CURRENT_LIST_DIR \"${{CMAKE_CURRENT_LIST_FILE}}\" DIRECTORY)\n"
                           "{}\n"
                           "endif()\n",
                           p.second,
                           path_to_triplet.generic_u8string(),
                           fs.read_contents(path_to_triplet, VCPKG_LINE_INFO));
        }

        extraction_file.append(
            R"(
set(CMAKE_CURRENT_LIST_FILE "${_vcpkg_triplet_file_BACKUP_CURRENT_LIST_FILE}")
get_filename_component(CMAKE_CURRENT_LIST_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
endmacro()
)");
        return extraction_file;
    }

    Path TripletCMakeVarProvider::create_tag_extraction_file(
        const View<std::pair<FullPackageSpec, std::string>> spec_abi_settings) const
    {
        const Filesystem& fs = paths.get_filesystem();
        static int tag_extract_id = 0;

        std::map<Triplet, int> emitted_triplets;
        int emitted_triplet_id = 0;
        for (const auto& spec_abi_setting : spec_abi_settings)
        {
            emitted_triplets[spec_abi_setting.first.package_spec.triplet()] = emitted_triplet_id++;
        }
        std::string extraction_file = create_extraction_file_prelude(paths, emitted_triplets);

        // The variables collected here are those necessary to perform builds.
        extraction_file.append(R"(

function(vcpkg_get_tags PORT FEATURES VCPKG_TRIPLET_ID VCPKG_ABI_SETTINGS_FILE)
    message("d8187afd-ea4a-4fc3-9aa4-a6782e1ed9af")
    vcpkg_triplet_file(${VCPKG_TRIPLET_ID})

    # GUID used as a flag - "cut here line"
    message("c35112b6-d1ba-415b-aa5d-81de856ef8eb
VCPKG_TARGET_ARCHITECTURE=${VCPKG_TARGET_ARCHITECTURE}
VCPKG_CMAKE_SYSTEM_NAME=${VCPKG_CMAKE_SYSTEM_NAME}
VCPKG_CMAKE_SYSTEM_VERSION=${VCPKG_CMAKE_SYSTEM_VERSION}
VCPKG_PLATFORM_TOOLSET=${VCPKG_PLATFORM_TOOLSET}
VCPKG_PLATFORM_TOOLSET_VERSION=${VCPKG_PLATFORM_TOOLSET_VERSION}
VCPKG_VISUAL_STUDIO_PATH=${VCPKG_VISUAL_STUDIO_PATH}
VCPKG_CHAINLOAD_TOOLCHAIN_FILE=${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}
VCPKG_BUILD_TYPE=${VCPKG_BUILD_TYPE}
VCPKG_LIBRARY_LINKAGE=${VCPKG_LIBRARY_LINKAGE}
VCPKG_CRT_LINKAGE=${VCPKG_CRT_LINKAGE}
e1e74b5c-18cb-4474-a6bd-5c1c8bc81f3f")

    # Just to enforce the user didn't set it in the triplet file
    if (DEFINED VCPKG_PUBLIC_ABI_OVERRIDE)
        set(VCPKG_PUBLIC_ABI_OVERRIDE)
        message(WARNING "VCPKG_PUBLIC_ABI_OVERRIDE set in the triplet will be ignored.")
    endif()
    include("${VCPKG_ABI_SETTINGS_FILE}" OPTIONAL)

    message("c35112b6-d1ba-415b-aa5d-81de856ef8eb
VCPKG_PUBLIC_ABI_OVERRIDE=${VCPKG_PUBLIC_ABI_OVERRIDE}
VCPKG_ENV_PASSTHROUGH=${VCPKG_ENV_PASSTHROUGH}
VCPKG_ENV_PASSTHROUGH_UNTRACKED=${VCPKG_ENV_PASSTHROUGH_UNTRACKED}
VCPKG_LOAD_VCVARS_ENV=${VCPKG_LOAD_VCVARS_ENV}
VCPKG_DISABLE_COMPILER_TRACKING=${VCPKG_DISABLE_COMPILER_TRACKING}
VCPKG_HASH_ADDITIONAL_FILES=${VCPKG_HASH_ADDITIONAL_FILES}
VCPKG_POST_PORTFILE_INCLUDES=${VCPKG_POST_PORTFILE_INCLUDES}
VCPKG_XBOX_CONSOLE_TARGET=${VCPKG_XBOX_CONSOLE_TARGET}
Z_VCPKG_GameDKLatest=$ENV{GameDKLatest}
e1e74b5c-18cb-4474-a6bd-5c1c8bc81f3f
8c504940-be29-4cba-9f8f-6cd83e9d87b7")
endfunction()
)");

        for (const auto& spec_abi_setting : spec_abi_settings)
        {
            const FullPackageSpec& spec = spec_abi_setting.first;

            std::string featurelist;
            for (auto&& f : spec.features)
            {
                if (f == FeatureNameCore || f == FeatureNameDefault || f == "*") continue;
                if (!featurelist.empty()) featurelist.push_back(';');
                featurelist.append(f);
            }

            fmt::format_to(std::back_inserter(extraction_file),
                           "vcpkg_get_tags(\"{}\" \"{}\" \"{}\" \"{}\")\n",
                           spec.package_spec.name(),
                           featurelist,
                           emitted_triplets[spec.package_spec.triplet()],
                           spec_abi_setting.second);
        }

        auto tags_path = paths.buildtrees() / fmt::format("{}.vcpkg_tags.cmake", tag_extract_id++);
        fs.write_contents_and_dirs(tags_path, extraction_file, VCPKG_LINE_INFO);
        return tags_path;
    }

    Path TripletCMakeVarProvider::create_dep_info_extraction_file(const View<PackageSpec> specs) const
    {
        static int dep_info_id = 0;
        const Filesystem& fs = paths.get_filesystem();

        std::map<Triplet, int> emitted_triplets;
        int emitted_triplet_id = 0;
        for (const auto& spec : specs)
        {
            emitted_triplets[spec.triplet()] = emitted_triplet_id++;
        }

        std::string extraction_file = create_extraction_file_prelude(paths, emitted_triplets);

        // The variables collected here are those necessary to perform dependency resolution.
        // If a value affects platform expressions, it must be here.
        extraction_file.append(R"(

function(vcpkg_get_dep_info PORT VCPKG_TRIPLET_ID)
    message("d8187afd-ea4a-4fc3-9aa4-a6782e1ed9af")
    vcpkg_triplet_file(${VCPKG_TRIPLET_ID})

    # GUID used as a flag - "cut here line"
    message("c35112b6-d1ba-415b-aa5d-81de856ef8eb
VCPKG_TARGET_ARCHITECTURE=${VCPKG_TARGET_ARCHITECTURE}
VCPKG_CMAKE_SYSTEM_NAME=${VCPKG_CMAKE_SYSTEM_NAME}
VCPKG_CMAKE_SYSTEM_VERSION=${VCPKG_CMAKE_SYSTEM_VERSION}
VCPKG_LIBRARY_LINKAGE=${VCPKG_LIBRARY_LINKAGE}
VCPKG_CRT_LINKAGE=${VCPKG_CRT_LINKAGE}
VCPKG_DEP_INFO_OVERRIDE_VARS=${VCPKG_DEP_INFO_OVERRIDE_VARS}
CMAKE_HOST_SYSTEM_NAME=${CMAKE_HOST_SYSTEM_NAME}
CMAKE_HOST_SYSTEM_PROCESSOR=${CMAKE_HOST_SYSTEM_PROCESSOR}
CMAKE_HOST_SYSTEM_VERSION=${CMAKE_HOST_SYSTEM_VERSION}
CMAKE_HOST_SYSTEM=${CMAKE_HOST_SYSTEM}
VCPKG_XBOX_CONSOLE_TARGET=${VCPKG_XBOX_CONSOLE_TARGET}
e1e74b5c-18cb-4474-a6bd-5c1c8bc81f3f
8c504940-be29-4cba-9f8f-6cd83e9d87b7")
endfunction()
)");

        for (const PackageSpec& spec : specs)
        {
            const auto& spec_name = spec.name();
            // Note that "_manifest_" is valid as a CMake parameter name, but isn't
            // a valid name of a real port.
            static constexpr StringLiteral manifest_port_name = "_manifest_";
            StringView vcpkg_get_dep_info_name;
            if (spec_name.empty())
            {
                vcpkg_get_dep_info_name = manifest_port_name;
            }
            else
            {
                vcpkg_get_dep_info_name = spec_name;
            }

            fmt::format_to(std::back_inserter(extraction_file),
                           "vcpkg_get_dep_info({} {})\n",
                           vcpkg_get_dep_info_name,
                           emitted_triplets[spec.triplet()]);
        }

        auto dep_info_path = paths.buildtrees() / fmt::format("{}.vcpkg_dep_info.cmake", dep_info_id++);
        fs.write_contents_and_dirs(dep_info_path, extraction_file, VCPKG_LINE_INFO);
        return dep_info_path;
    }

    void TripletCMakeVarProvider::launch_and_split(
        const Path& script_path, std::vector<std::vector<std::pair<std::string, std::string>>>& vars) const
    {
        static constexpr StringLiteral PORT_START_GUID = "d8187afd-ea4a-4fc3-9aa4-a6782e1ed9af";
        static constexpr StringLiteral PORT_END_GUID = "8c504940-be29-4cba-9f8f-6cd83e9d87b7";
        static constexpr StringLiteral BLOCK_START_GUID = "c35112b6-d1ba-415b-aa5d-81de856ef8eb";
        static constexpr StringLiteral BLOCK_END_GUID = "e1e74b5c-18cb-4474-a6bd-5c1c8bc81f3f";

        auto cmd = vcpkg::make_cmake_cmd(paths, script_path, {});

        std::vector<std::string> lines;
        auto const exit_code = cmd_execute_and_stream_lines(cmd, [&](StringView sv) {
                                   lines.emplace_back(sv.begin(), sv.end());
                               }).value_or_exit(VCPKG_LINE_INFO);

        if (exit_code != 0)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                          msg::format(msgCommandFailed, msg::command_line = cmd.command_line())
                                              .append_raw('\n')
                                              .append_raw(Strings::join(", ", lines)));
        }

        const auto end = lines.cend();

        auto port_start = std::find(lines.cbegin(), end, PORT_START_GUID);
        auto port_end = std::find(port_start, end, PORT_END_GUID);
        Checks::msg_check_exit(VCPKG_LINE_INFO, port_start != end && port_end != end, msgFailedToParseCMakeConsoleOut);

        for (auto var_itr = vars.begin(); port_start != end && var_itr != vars.end(); ++var_itr)
        {
            auto block_start = std::find(port_start, port_end, BLOCK_START_GUID);
            Checks::msg_check_exit(VCPKG_LINE_INFO, block_start != port_end, msgFailedToParseCMakeConsoleOut);
            auto block_end = std::find(++block_start, port_end, BLOCK_END_GUID);

            while (block_start != port_end)
            {
                while (block_start != block_end)
                {
                    const std::string& line = *block_start;

                    std::vector<std::string> s = Strings::split(line, '=');
                    Checks::msg_check_exit(VCPKG_LINE_INFO,
                                           s.size() == 1 || s.size() == 2,
                                           msgUnexpectedFormat,
                                           msg::expected = "VARIABLE_NAME=VARIABLE_VALUE",
                                           msg::actual = line);

                    var_itr->emplace_back(std::move(s[0]), s.size() == 1 ? "" : std::move(s[1]));

                    ++block_start;
                }

                block_start = std::find(block_end, port_end, BLOCK_START_GUID);
                block_end = std::find(block_start, port_end, BLOCK_END_GUID);
            }

            port_start = std::find(port_end, end, PORT_START_GUID);
            port_end = std::find(port_start, end, PORT_END_GUID);
        }
    }

    void TripletCMakeVarProvider::load_generic_triplet_vars(Triplet triplet) const
    {
        std::vector<std::vector<std::pair<std::string, std::string>>> vars(1);
        // Hack: PackageSpecs should never have .name==""
        std::pair<FullPackageSpec, std::string> tag_extracts{FullPackageSpec{{"", triplet}, {}}, ""};
        const auto file_path =
            create_tag_extraction_file(View<std::pair<FullPackageSpec, std::string>>{&tag_extracts, 1});
        launch_and_split(file_path, vars);
        paths.get_filesystem().remove(file_path, VCPKG_LINE_INFO);

        generic_triplet_vars[triplet].insert(std::make_move_iterator(vars.front().begin()),
                                             std::make_move_iterator(vars.front().end()));
    }

    void TripletCMakeVarProvider::load_dep_info_vars(View<PackageSpec> original_specs, Triplet host_triplet) const
    {
        std::vector<PackageSpec> specs = Util::filter(original_specs, [this](const PackageSpec& spec) {
            return dep_resolution_vars.find(spec) == dep_resolution_vars.end();
        });
        if (specs.size() == 0) return;
        Debug::println("Loading dep info for: ", Strings::join(" ", specs));
        std::vector<std::vector<std::pair<std::string, std::string>>> vars(specs.size());
        const auto file_path = create_dep_info_extraction_file(specs);
        if (specs.size() > 100)
        {
            msg::println(msgLoadingDependencyInformation, msg::count = specs.size());
        }
        launch_and_split(file_path, vars);
        paths.get_filesystem().remove(file_path, VCPKG_LINE_INFO);

        auto var_list_itr = vars.begin();
        for (const PackageSpec& spec : specs)
        {
            PlatformExpression::Context ctxt{std::make_move_iterator(var_list_itr->begin()),
                                             std::make_move_iterator(var_list_itr->end())};
            ++var_list_itr;

            ctxt.emplace("Z_VCPKG_IS_NATIVE", host_triplet == spec.triplet() ? "1" : "0");

            dep_resolution_vars.emplace(spec, std::move(ctxt));
        }
    }

    void TripletCMakeVarProvider::load_tag_vars(View<FullPackageSpec> specs,
                                                View<Path> port_locations,
                                                Triplet host_triplet) const
    {
        if (specs.empty()) return;
        std::vector<std::pair<FullPackageSpec, std::string>> spec_abi_settings;
        spec_abi_settings.reserve(specs.size());
        Checks::check_exit(VCPKG_LINE_INFO, specs.size() == port_locations.size());

        for (size_t i = 0; i < specs.size(); ++i)
        {
            const auto override_path = port_locations[i] / "vcpkg-abi-settings.cmake";
            spec_abi_settings.emplace_back(specs[i], override_path.generic_u8string());
        }

        std::vector<std::vector<std::pair<std::string, std::string>>> vars(spec_abi_settings.size());
        const auto file_path = create_tag_extraction_file(spec_abi_settings);
        launch_and_split(file_path, vars);
        paths.get_filesystem().remove(file_path, VCPKG_LINE_INFO);

        auto var_list_itr = vars.begin();
        for (const auto& spec_abi_setting : spec_abi_settings)
        {
            const FullPackageSpec& spec = spec_abi_setting.first;
            PlatformExpression::Context ctxt{std::make_move_iterator(var_list_itr->begin()),
                                             std::make_move_iterator(var_list_itr->end())};
            ++var_list_itr;

            ctxt.emplace("Z_VCPKG_IS_NATIVE", host_triplet == spec.package_spec.triplet() ? "1" : "0");

            tag_vars.emplace(spec.package_spec, std::move(ctxt));
        }
    }

    Optional<const std::unordered_map<std::string, std::string>&> TripletCMakeVarProvider::get_generic_triplet_vars(
        Triplet triplet) const
    {
        return Util::lookup_value(generic_triplet_vars, triplet);
    }

    Optional<const std::unordered_map<std::string, std::string>&> TripletCMakeVarProvider::get_dep_info_vars(
        const PackageSpec& spec) const
    {
        return Util::lookup_value(dep_resolution_vars, spec);
    }

    Optional<const std::unordered_map<std::string, std::string>&> TripletCMakeVarProvider::get_tag_vars(
        const PackageSpec& spec) const
    {
        return Util::lookup_value(tag_vars, spec);
    }
}
