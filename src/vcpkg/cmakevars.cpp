#include <vcpkg/base/hash.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/buildenvironment.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/vcpkgpaths.h>

#include <numeric>

using namespace vcpkg;
namespace vcpkg::CMakeVars
{
    void CMakeVarProvider::load_tag_vars(const ActionPlan& action_plan,
                                         const PortFileProvider& port_provider,
                                         Triplet host_triplet) const
    {
        std::vector<FullPackageSpec> install_package_specs;
        install_package_specs.reserve(action_plan.install_actions.size());
        for (auto&& action : action_plan.install_actions)
        {
            install_package_specs.emplace_back(FullPackageSpec{action.spec, action.feature_list});
        }

        load_tag_and_triplet_vars(install_package_specs, port_provider, host_triplet);
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

            void load_tag_and_triplet_vars(View<FullPackageSpec> specs,
                                           const PortFileProvider& port_provider,
                                           Triplet host_triplet) const override;

            Optional<const std::unordered_map<std::string, std::string>&> get_generic_triplet_vars(
                Triplet triplet) const override;

            Optional<const std::unordered_map<std::string, std::string>&> get_dep_info_vars(
                const PackageSpec& spec) const override;

            Optional<const std::unordered_map<std::string, std::string>&> get_tag_vars(
                const PackageSpec& spec) const override;
            Optional<const std::unordered_map<std::string, std::string>&> get_triplet_vars(
                const PackageSpec& spec) const override;

            CMakeTraceOutput parse_cmake_trace(const std::vector<std::string>& trace_lines) const;

            void analyze_cmake_trace(const CMakeTraceOutput& trace,
                                     std::vector<std::unordered_map<std::string, std::string>>& result) const;

        public:
            Path create_tag_extraction_file(
                const View<std::pair<const FullPackageSpec*, std::string>> spec_abi_settings) const;

            Path create_dep_info_extraction_file(const View<PackageSpec> specs) const;

            void launch_and_split(const Path& script_path,
                                  std::vector<std::vector<std::pair<std::string, std::string>>>& vars,
                                  Optional<std::vector<std::unordered_map<std::string, std::string>>&>
                                      opt_triplet_hashes = nullopt) const;

            const VcpkgPaths& paths;
            mutable std::unordered_map<PackageSpec, std::unordered_map<std::string, std::string>> dep_resolution_vars;
            mutable std::unordered_map<PackageSpec, std::unordered_map<std::string, std::string>> tag_vars;
            mutable std::unordered_map<PackageSpec, std::unordered_map<std::string, std::string>>
                triplet_vars; // I feel like I could also add --x-cmake-args into this variable
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

        Strings::append(extraction_file,
                        "cmake_minimum_required(VERSION 3.5)\n"
                        "macro(vcpkg_triplet_file VCPKG_TRIPLET_ID)\n",
                        "set(_vcpkg_triplet_file_BACKUP_CURRENT_LIST_FILE \"${CMAKE_CURRENT_LIST_FILE}\")\n");

        for (auto&& p : emitted_triplets)
        {
            auto path_to_triplet = paths.get_triplet_file_path(p.first);
            Strings::append(extraction_file, "if(VCPKG_TRIPLET_ID EQUAL ", p.second, ")\n");
            Strings::append(
                extraction_file, "set(CMAKE_CURRENT_LIST_FILE \"", path_to_triplet.generic_u8string(), "\")\n");
            Strings::append(
                extraction_file,
                "get_filename_component(CMAKE_CURRENT_LIST_DIR \"${CMAKE_CURRENT_LIST_FILE}\" DIRECTORY)\n");
            Strings::append(extraction_file, "message(\"start-triplet-contents-0123\")\n");
            Strings::append(extraction_file, fs.read_contents(path_to_triplet, VCPKG_LINE_INFO));
            Strings::append(extraction_file, "\nmessage(\"end-triplet-contents-3210\")\n");
            Strings::append(extraction_file, "endif()\n");
        }
        Strings::append(extraction_file,
                        R"(
set(CMAKE_CURRENT_LIST_FILE "${_vcpkg_triplet_file_BACKUP_CURRENT_LIST_FILE}")
unset(_vcpkg_triplet_file_BACKUP_CURRENT_LIST_FILE)
get_filename_component(CMAKE_CURRENT_LIST_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
endmacro()
)");
        return extraction_file;
    }

    Path TripletCMakeVarProvider::create_tag_extraction_file(
        const View<std::pair<const FullPackageSpec*, std::string>> spec_abi_settings) const
    {
        Filesystem& fs = paths.get_filesystem();
        static int tag_extract_id = 0;

        std::map<Triplet, int> emitted_triplets;
        int emitted_triplet_id = 0;
        for (const auto& spec_abi_setting : spec_abi_settings)
        {
            emitted_triplets[spec_abi_setting.first->package_spec.triplet()] = emitted_triplet_id++;
        }
        std::string extraction_file = create_extraction_file_prelude(paths, emitted_triplets);

        Strings::append(extraction_file, R"(

function(vcpkg_get_tags PORT FEATURES VCPKG_TRIPLET_ID VCPKG_ABI_SETTINGS_FILE)
    message("\nd8187afd-ea4a-4fc3-9aa4-a6782e1ed9af\n")
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
e1e74b5c-18cb-4474-a6bd-5c1c8bc81f3f
8c504940-be29-4cba-9f8f-6cd83e9d87b7")
endfunction()
)");

        for (const auto& spec_abi_setting : spec_abi_settings)
        {
            const FullPackageSpec& spec = *spec_abi_setting.first;

            std::string featurelist;
            for (auto&& f : spec.features)
            {
                if (f == "core" || f == "default" || f == "*") continue;
                if (!featurelist.empty()) featurelist.push_back(';');
                featurelist.append(f);
            }

            Strings::append(extraction_file,
                            "vcpkg_get_tags(\"",
                            spec.package_spec.name(),
                            "\" \"",
                            featurelist,
                            "\" \"",
                            emitted_triplets[spec.package_spec.triplet()],
                            "\" \"",
                            spec_abi_setting.second,
                            "\")\n");
        }

        auto tags_path = paths.buildtrees() / Strings::concat(tag_extract_id++, ".vcpkg_tags.cmake");
        fs.write_contents_and_dirs(tags_path, extraction_file, VCPKG_LINE_INFO);
        return tags_path;
    }

    Path TripletCMakeVarProvider::create_dep_info_extraction_file(const View<PackageSpec> specs) const
    {
        static int dep_info_id = 0;
        Filesystem& fs = paths.get_filesystem();

        std::map<Triplet, int> emitted_triplets;
        int emitted_triplet_id = 0;
        for (const auto& spec : specs)
        {
            emitted_triplets[spec.triplet()] = emitted_triplet_id++;
        }

        std::string extraction_file = create_extraction_file_prelude(paths, emitted_triplets);

        Strings::append(extraction_file, R"(

function(vcpkg_get_dep_info PORT VCPKG_TRIPLET_ID)
    message("\nd8187afd-ea4a-4fc3-9aa4-a6782e1ed9af\n")
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

            Strings::append(extraction_file,
                            "vcpkg_get_dep_info(",
                            vcpkg_get_dep_info_name,
                            " ",
                            emitted_triplets[spec.triplet()],
                            ")\n");
        }

        auto dep_info_path = paths.buildtrees() / Strings::concat(dep_info_id++, ".vcpkg_dep_info.cmake");
        fs.write_contents_and_dirs(dep_info_path, extraction_file, VCPKG_LINE_INFO);
        return dep_info_path;
    }

    struct CMakeTraceVersionDeserializer : Json::IDeserializer<CMakeTraceVersion>
    {
        virtual StringView type_name() const override { return "a line of a cmake trace"; }

        virtual Optional<CMakeTraceVersion> visit_object(Json::Reader& r, const Json::Object& obj) override
        {
            Optional<CMakeTraceVersion> x;
            CMakeTraceVersion& ret = x.emplace();
            r.required_object_field(
                "the major version", obj, "major", ret.major, Json::NaturalNumberDeserializer::instance);
            r.required_object_field(
                "the minor version", obj, "minor", ret.minor, Json::NaturalNumberDeserializer::instance);

            return x;
        }
    };
    struct CMakeTraceLineDeserializer : Json::IDeserializer<CMakeTraceLine>
    {
        virtual StringView type_name() const override { return "a line of a cmake trace"; }

        virtual Optional<CMakeTraceLine> visit_object(Json::Reader& r, const Json::Object& obj) override
        {
            Optional<CMakeTraceLine> x;
            CMakeTraceLine& ret = x.emplace();
            // TODO: figure out what goes in here
            static Json::ArrayDeserializer<Json::StringDeserializer> args_des("an array of arguments",
                                                                              Json::StringDeserializer{"an argument"});
            static Json::StringDeserializer cmd_deserializer("the cmake command");
            static Json::StringDeserializer file_deserializer("the file executing the command");
            r.required_object_field("the arguments", obj, "args", ret.args, args_des);
            r.required_object_field("the command", obj, "cmd", ret.cmd, cmd_deserializer);
            r.required_object_field("the file name", obj, "file", ret.file, Json::PathDeserializer::instance);
            r.required_object_field(
                "the internal execution frame", obj, "frame", ret.frame, Json::NaturalNumberDeserializer::instance);
            r.required_object_field("the global execution frame",
                                    obj,
                                    "global_frame",
                                    ret.global_frame,
                                    Json::NaturalNumberDeserializer::instance);
            r.required_object_field(
                "the line number", obj, "line", ret.line, Json::NaturalNumberDeserializer::instance);
            r.optional_object_field(obj, "line_end", ret.line_end.emplace(), Json::NaturalNumberDeserializer::instance);
            r.required_object_field(
                "the execution time", obj, "time", ret.time, Json::RealNumberDeserializer::instance);
            return x;
        }
    };

    CMakeTraceOutput TripletCMakeVarProvider::parse_cmake_trace(const std::vector<std::string>& trace_lines) const
    {
        CMakeTraceOutput cmake_trace;
        cmake_trace.traces.reserve(trace_lines.size() - 1);
        Json::Reader reader;
        {
            CMakeTraceVersionDeserializer trace_version_des;
            const auto parsed_json_cmake_version_opt = Json::parse(*trace_lines.begin());
            const auto& parsed_json_cmake_version = parsed_json_cmake_version_opt.value_or_exit(VCPKG_LINE_INFO).first;
            const auto parsed_json_cmake_version_obj = parsed_json_cmake_version.object(VCPKG_LINE_INFO);
            auto cmake_ver_opt = reader.visit(*parsed_json_cmake_version_obj.get("version"), trace_version_des);
            cmake_trace.version = std::move(cmake_ver_opt.value_or_exit(VCPKG_LINE_INFO));
        }

        {
            CMakeTraceLineDeserializer trace_line_des;
            for (auto trace_line_it = std::next(trace_lines.begin()); trace_line_it != trace_lines.end() - 1;
                 ++trace_line_it)
            // trace_lines.end()-1 : since trace always ends with a blank line which cannot be parsed.
            {
                const auto parsed_json_cmake_trace_line_opt = Json::parse(*trace_line_it);
                const auto& parsed_json_cmake_trace_line =
                    parsed_json_cmake_trace_line_opt.value_or_exit(VCPKG_LINE_INFO).first;
                const auto parsed_json_cmake_trace_line_obj = parsed_json_cmake_trace_line.object(VCPKG_LINE_INFO);
                const auto cmake_trace_opt = reader.visit(parsed_json_cmake_trace_line_obj, trace_line_des);
                cmake_trace.traces.emplace_back(std::move(cmake_trace_opt.value_or_exit(VCPKG_LINE_INFO)));
            }
        }
        return cmake_trace;
    }

    void TripletCMakeVarProvider::analyze_cmake_trace(
        const CMakeTraceOutput& cmake_trace, std::vector<std::unordered_map<std::string, std::string>>& result) const
    {
        // Basic trace order:
        //  cmd: vcpkg_get_tags or vcpkg_get_dep_info
        //  cmd message: (triplet start) d8187afd-ea4a-4fc3-9aa4-a6782e1ed9af PORT_START_GUID - single argument
        //  cmd: vcpkg_triplet_file
        //  cmd: message(\"start-triplet-contents-0123\")
        //  <triplet> <- That is what we want
        //  cmd: message(\"end-triplet-contents-3210\")
        //  cmd: message: (triplet end) c35112b6-d1ba-415b-aa5d-81de856ef8eb BLOCK_START_GUID - first argument
        //  BLOCK_END_GUID
        //  PORT_END_GUID - last argument
        //  <repeat>

        auto is_vcpkg_get_tags = [](const CMakeTraceLine& t) { return (t.cmd.compare("vcpkg_get_tags") == 0); };
        auto is_vcpkg_get_dep_info = [](const CMakeTraceLine& t) { return (t.cmd.compare("vcpkg_get_dep_info") == 0); };

        auto is_vcpkg_get_tags_or_dep_info = [&](const CMakeTraceLine& t) {
            return (is_vcpkg_get_tags(t) || is_vcpkg_get_dep_info(t));
        };

        // Could be used to minimize the search space
        // auto is_vcpkg_triplet_file = [](const CMakeTraceLine& t) {
        //    return (t.cmd.compare("vcpkg_triplet_file") == 0);
        //};
        auto is_message = [](const CMakeTraceLine& t) { return (t.cmd.compare("message") == 0); };
        auto is_message_triplet_start = [&](const CMakeTraceLine& t) {
            return (is_message(t) && (t.args.at(0).compare("start-triplet-contents-0123") == 0));
        };
        auto is_message_triplet_end = [&](const CMakeTraceLine& t) {
            return (is_message(t) && (t.args.at(0).compare("end-triplet-contents-3210") == 0));
        };
        auto is_relevant_command = [](const CMakeTraceLine& t) {
            static std::vector<std::string> cmake_commands{"set",
                                                           "unset",
                                                           "cmake_path",
                                                           "execute_process",
                                                           "file",
                                                           "find_file",
                                                           "find_library",
                                                           "find_path",
                                                           "find_program",
                                                           "get_cmake_property",
                                                           "get_directory_property",
                                                           "get_filename_component",
                                                           "get_property",
                                                           "list",
                                                           "math",
                                                           "option",
                                                           "separate_arguments",
                                                           "string",
                                                           "site_name"};
            for (const auto& to_compare : cmake_commands)
                if (t.cmd.compare(to_compare) == 0) return true;

            return false;
        };
        auto is_cmd_unset = [](const CMakeTraceLine& t) { return (t.cmd.compare("unset") == 0); };

        const auto trace_end = cmake_trace.traces.end();
        // Find first call block
        auto tags_or_deps_iter_begin =
            std::find_if(cmake_trace.traces.begin(), trace_end, is_vcpkg_get_tags_or_dep_info);

        while (tags_or_deps_iter_begin != trace_end)
        {
            auto tags_or_deps_iter_end =
                std::find_if(std::next(tags_or_deps_iter_begin), trace_end, is_vcpkg_get_tags_or_dep_info);

            // Find triplet block
            auto triplet_start_iter =
                std::find_if(tags_or_deps_iter_begin, tags_or_deps_iter_end, is_message_triplet_start);
            auto triplet_end_iter =
                std::find_if(tags_or_deps_iter_begin, tags_or_deps_iter_end, is_message_triplet_end);

            // Find all sets and unset in the triplet block:
            std::unordered_map<std::string, std::string> port_triplet_vars;
            for (auto var_set_searcher = std::find_if(triplet_start_iter, triplet_end_iter, is_relevant_command);
                 var_set_searcher != triplet_end_iter;
                 var_set_searcher = std::find_if(std::next(var_set_searcher), triplet_end_iter, is_relevant_command))
            {
                const auto trace_relevant = *var_set_searcher;

                if (is_cmd_unset(trace_relevant))
                {
                    const auto var_name = trace_relevant.args[0];
                    if (port_triplet_vars.find(var_name) != port_triplet_vars.end()) // contains is c++20
                    {
                        [[maybe_unused]] const auto throw_away = port_triplet_vars.extract(var_name);
                    }
                    else if (var_name.substr(0, 4).compare("ENV{"))
                    {
                        port_triplet_vars.insert_or_assign(var_name, "unset");
                    }
                }
                else
                {
                    std::vector<std::string> var_names;
                    if ((trace_relevant.cmd.compare("set") == 0) || (trace_relevant.cmd.compare("option") == 0) ||
                        (trace_relevant.cmd.compare("separate_arguments") == 0) ||
                        (trace_relevant.cmd.compare("site_name") == 0) ||
                        // get_cmake_property, get_directory_property, get_filename_component, get_property
                        (trace_relevant.cmd.substr(0, 4).compare("get_") == 0) ||
                        // find_file, find_library, find_path, find_program
                        (trace_relevant.cmd.substr(0, 5).compare("find_") == 0))
                    {
                        var_names.emplace_back(trace_relevant.args[0]);
                    }
                    else if (trace_relevant.cmd.compare("cmake_path") == 0)
                    {
                        if (trace_relevant.args[0].compare("SET") == 0)
                        {
                            var_names.emplace_back(trace_relevant.args[1]);
                        }
                        else if (trace_relevant.args[0].compare("GET") == 0)
                        {
                            var_names.emplace_back(*trace_relevant.args.end());
                        }
                        else if (trace_relevant.args[0].compare("COMPARE") == 0 ||
                                 trace_relevant.args[0].compare("HASH") == 0 ||
                                 trace_relevant.args[0].compare("NATIVE_PATH") == 0 ||
                                 trace_relevant.args[0].substr(0, 3).compare("IS_") == 0 ||
                                 trace_relevant.args[0].substr(0, 4).compare("HAS_") == 0)
                        {
                            var_names.emplace_back(*trace_relevant.args.end());
                        }
                        else if (trace_relevant.args[0].compare("CONVERT") == 0)
                        {
                            var_names.emplace_back(trace_relevant.args[3]);
                        }
                        else if (trace_relevant.args[0].compare("NORMAL_PATH") == 0 ||
                                 trace_relevant.args[0].compare("CONVERT") == 0 ||
                                 trace_relevant.args[0].compare("APPEND") == 0 ||
                                 trace_relevant.args[0].compare("APPEND_STRING") == 0 ||
                                 trace_relevant.args[0].compare("REMOVE_FILENAME") == 0 ||
                                 trace_relevant.args[0].compare("REPLACE_FILENAME") == 0 ||
                                 trace_relevant.args[0].compare("REMOVE_EXTENSION") == 0 ||
                                 trace_relevant.args[0].compare("REPLACE_EXTENSION") == 0)
                        {
                            auto output_var =
                                std::find(trace_relevant.args.begin(), trace_relevant.args.end(), "OUTPUT_VARIABLE");
                            if (output_var != trace_relevant.args.end())
                            {
                                var_names.emplace_back(*(++output_var));
                            }
                            else
                            {
                                var_names.emplace_back(trace_relevant.args[1]);
                            }
                        }
                    }
                    else if (trace_relevant.cmd.compare("execute_process") == 0)
                    {
                        auto output_var =
                            std::find(trace_relevant.args.begin(), trace_relevant.args.end(), "OUTPUT_VARIABLE");
                        if (output_var != trace_relevant.args.end())
                        {
                            var_names.emplace_back(*(++output_var));
                        }
                        output_var =
                            std::find(trace_relevant.args.begin(), trace_relevant.args.end(), "RESULT_VARIABLE");
                        if (output_var != trace_relevant.args.end())
                        {
                            var_names.emplace_back(*(++output_var));
                        }
                        output_var =
                            std::find(trace_relevant.args.begin(), trace_relevant.args.end(), "RESULTS_VARIABLE");
                        if (output_var != trace_relevant.args.end())
                        {
                            var_names.emplace_back(*(++output_var));
                        }
                        output_var =
                            std::find(trace_relevant.args.begin(), trace_relevant.args.end(), "ERROR_VARIABLE");
                        if (output_var != trace_relevant.args.end())
                        {
                            var_names.emplace_back(*(++output_var));
                        }
                    }
                    else if (trace_relevant.cmd.compare("file") == 0)
                    {
                        if (trace_relevant.args[0].compare("READ") == 0 ||
                            trace_relevant.args[0].compare("STRINGS") == 0 ||
                            trace_relevant.args[0].compare("MD5") == 0 || trace_relevant.args[0].compare("SHA1") == 0 ||
                            trace_relevant.args[0].compare("SHA224") == 0 ||
                            trace_relevant.args[0].compare("SHA256") == 0 ||
                            trace_relevant.args[0].compare("SHA384") == 0 ||
                            trace_relevant.args[0].compare("SHA512") == 0 ||
                            trace_relevant.args[0].compare("SHA3_224") == 0 ||
                            trace_relevant.args[0].compare("SHA3_256") == 0 ||
                            trace_relevant.args[0].compare("SHA3_384") == 0 ||
                            trace_relevant.args[0].compare("SHA3_512") == 0 ||
                            trace_relevant.args[0].compare("TIMESTAMP") == 0 ||
                            trace_relevant.args[0].compare("SIZE") == 0 ||
                            trace_relevant.args[0].compare("READ_SYMLINK") == 0 ||
                            trace_relevant.args[0].compare("REAL_PATH") == 0 ||
                            trace_relevant.args[0].compare("TO_CMAKE_PATH") == 0 ||
                            trace_relevant.args[0].compare("TO_NATIVE_PATH") == 0)
                        {
                            var_names.emplace_back(trace_relevant.args[2]);
                        }
                        else if (trace_relevant.args[0].compare("GLOB") == 0 ||
                                 trace_relevant.args[0].compare("GLOB_RECURSE") == 0 ||
                                 trace_relevant.args[0].compare("RELATIVE_PATH") == 0)
                        {
                            var_names.emplace_back(trace_relevant.args[1]);
                        }
                    }
                    else if (trace_relevant.cmd.compare("list") == 0)
                    {
                        var_names.emplace_back(trace_relevant.args[1]);
                        if (trace_relevant.args[0].substr(0, 4).compare("POP_") && trace_relevant.args.size() >= 3)
                        {
                            // POP_FRONT|BACK
                            for (auto out_vars_iter = (trace_relevant.args.begin() + 2);
                                 out_vars_iter != trace_relevant.args.end();
                                 ++out_vars_iter)
                            {
                                var_names.emplace_back(*out_vars_iter);
                            }
                        }
                    }
                    else if (trace_relevant.cmd.compare("math") == 0)
                    {
                        var_names.emplace_back(trace_relevant.args[1]);
                    }
                    else if (trace_relevant.cmd.compare("string") == 0)
                    {
                        if (trace_relevant.args[0].compare("FIND") == 0 ||
                            trace_relevant.args[0].compare("REPLACE") == 0 ||
                            trace_relevant.args[0].compare("REPEAT") == 0)
                        {
                            var_names.emplace_back(trace_relevant.args[3]);
                        }
                        else if (trace_relevant.args[0].compare("JSON") == 0)
                        {
                            var_names.emplace_back(trace_relevant.args[1]);
                            auto output_var =
                                std::find(trace_relevant.args.begin(), trace_relevant.args.end(), "ERROR_VARIABLE");
                            if (output_var != trace_relevant.args.end())
                            {
                                var_names.emplace_back(*(++output_var));
                            }
                        }
                        else if (trace_relevant.args[0].compare("MD5") == 0 ||
                                 trace_relevant.args[0].compare("SHA1") == 0 ||
                                 trace_relevant.args[0].compare("SHA224") == 0 ||
                                 trace_relevant.args[0].compare("SHA256") == 0 ||
                                 trace_relevant.args[0].compare("SHA384") == 0 ||
                                 trace_relevant.args[0].compare("SHA512") == 0 ||
                                 trace_relevant.args[0].compare("SHA3_224") == 0 ||
                                 trace_relevant.args[0].compare("SHA3_256") == 0 ||
                                 trace_relevant.args[0].compare("SHA3_384") == 0 ||
                                 trace_relevant.args[0].compare("SHA3_512") == 0 ||
                                 trace_relevant.args[0].compare("TIMESTAMP") == 0 ||
                                 trace_relevant.args[0].compare("CONCAT"))
                        {
                            var_names.emplace_back(trace_relevant.args[1]);
                        }
                        // TODO: a lot of extra cases
                    }
                    else
                    {
                        assert(0);
                    }
                    auto args_concat = Strings::join(";", trace_relevant.args);
                    for (auto& var_name : var_names)
                    {
                        port_triplet_vars.insert_or_assign(
                            var_name, Strings::concat(trace_relevant.cmd, "(", std::move(args_concat), ")"));
                    }
                }
            };
            tags_or_deps_iter_begin = tags_or_deps_iter_end;
            result.emplace_back(std::move(port_triplet_vars));
        }
    }

    void TripletCMakeVarProvider::launch_and_split(
        const Path& script_path,
        std::vector<std::vector<std::pair<std::string, std::string>>>& vars,
        Optional<std::vector<std::unordered_map<std::string, std::string>>&> opt_triplet_vars) const
    {
        const auto& fs = paths.get_filesystem();

        static constexpr StringLiteral PORT_START_GUID = "d8187afd-ea4a-4fc3-9aa4-a6782e1ed9af";
        static constexpr StringLiteral PORT_END_GUID = "8c504940-be29-4cba-9f8f-6cd83e9d87b7";
        static constexpr StringLiteral BLOCK_START_GUID = "c35112b6-d1ba-415b-aa5d-81de856ef8eb";
        static constexpr StringLiteral BLOCK_END_GUID = "e1e74b5c-18cb-4474-a6bd-5c1c8bc81f3f";

        const auto trace_output = Path(script_path.parent_path()) / "0.vcpkg_tags.trace";
        const auto trace_redirect = std::string("--trace-redirect=") + trace_output.c_str();
        const auto cmd_launch_cmake =
            vcpkg::make_cmake_cmd(paths, script_path, {}, {{"--trace-format=json-v1"}, {trace_redirect}});
        // TODO: delete trace file after read!

        std::vector<std::string> lines;
        auto const exit_code = cmd_execute_and_stream_lines(
                                   cmd_launch_cmake,
                                   [&](StringView sv) { lines.emplace_back(sv.begin(), sv.end()); },
                                   default_working_directory)
                                   .value_or_exit(VCPKG_LINE_INFO);

        if (exit_code != 0)
        {
            Checks::msg_exit_with_message(
                VCPKG_LINE_INFO,
                msg::format(msgCommandFailed, msg::command_line = cmd_launch_cmake.command_line())
                    .append_raw('\n')
                    .append_raw(Strings::join(", ", lines)));
        }

        if (auto triplet_vars_out = opt_triplet_vars.get())
        {
            const auto trace_lines = fs.read_lines(trace_output, VCPKG_LINE_INFO);

            CMakeTraceOutput cmake_trace = parse_cmake_trace(trace_lines);
            // Parse (unexpanded) trace output
            analyze_cmake_trace(cmake_trace, *triplet_vars_out);
        }
        // Parse cmake message output (expanded)
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
        paths.get_filesystem().remove(trace_output, VCPKG_LINE_INFO);
    }

    void TripletCMakeVarProvider::load_generic_triplet_vars(Triplet triplet) const
    {
        std::vector<std::vector<std::pair<std::string, std::string>>> vars(1);
        // Hack: PackageSpecs should never have .name==""
        FullPackageSpec full_spec({"", triplet}, {});
        const auto file_path = create_tag_extraction_file(std::array<std::pair<const FullPackageSpec*, std::string>, 1>{
            std::pair<const FullPackageSpec*, std::string>{&full_spec, ""}});
        launch_and_split(file_path, vars);
        paths.get_filesystem().remove(file_path, VCPKG_LINE_INFO);

        generic_triplet_vars[triplet].insert(std::make_move_iterator(vars.front().begin()),
                                             std::make_move_iterator(vars.front().end()));
    }

    void TripletCMakeVarProvider::load_dep_info_vars(View<PackageSpec> specs, Triplet host_triplet) const
    {
        if (specs.size() == 0) return;
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

    void TripletCMakeVarProvider::load_tag_and_triplet_vars(View<FullPackageSpec> specs,
                                                            const PortFileProvider& port_provider,
                                                            Triplet host_triplet) const
    {
        if (specs.size() == 0) return;
        std::vector<std::pair<const FullPackageSpec*, std::string>> spec_abi_settings;
        spec_abi_settings.reserve(specs.size());
        std::vector<std::unordered_map<std::string, std::string>> triplet_vars_vec;
        triplet_vars_vec.reserve(specs.size());

        for (const FullPackageSpec& spec : specs)
        {
            auto& scfl = port_provider.get_control_file(spec.package_spec.name()).value_or_exit(VCPKG_LINE_INFO);
            const auto override_path = scfl.source_location / "vcpkg-abi-settings.cmake";
            spec_abi_settings.emplace_back(&spec, override_path.generic_u8string());
        }

        std::vector<std::vector<std::pair<std::string, std::string>>> vars(spec_abi_settings.size());
        const auto file_path = create_tag_extraction_file(spec_abi_settings);
        launch_and_split(file_path, vars, triplet_vars_vec);
        paths.get_filesystem().remove(file_path, VCPKG_LINE_INFO);

        auto var_list_itr = vars.begin();
        auto triplet_vars_iter = triplet_vars_vec.begin();
        for (const auto& spec_abi_setting : spec_abi_settings)
        {
            const FullPackageSpec& spec = *spec_abi_setting.first;
            PlatformExpression::Context ctxt{std::make_move_iterator(var_list_itr->begin()),
                                             std::make_move_iterator(var_list_itr->end())};
            ++var_list_itr;

            ctxt.emplace("Z_VCPKG_IS_NATIVE", host_triplet == spec.package_spec.triplet() ? "1" : "0");

            tag_vars.emplace(spec.package_spec, std::move(ctxt));
            triplet_vars.emplace(spec.package_spec, std::move(*triplet_vars_iter));
            ++triplet_vars_iter;
        }
    }

    // All those function below do the same....
    Optional<const std::unordered_map<std::string, std::string>&> TripletCMakeVarProvider::get_generic_triplet_vars(
        Triplet triplet) const
    {
        auto find_itr = generic_triplet_vars.find(triplet);
        if (find_itr != generic_triplet_vars.end())
        {
            return find_itr->second;
        }

        return nullopt;
    }

    Optional<const std::unordered_map<std::string, std::string>&> TripletCMakeVarProvider::get_dep_info_vars(
        const PackageSpec& spec) const
    {
        auto find_itr = dep_resolution_vars.find(spec);
        if (find_itr != dep_resolution_vars.end())
        {
            return find_itr->second;
        }

        return nullopt;
    }

    Optional<const std::unordered_map<std::string, std::string>&> TripletCMakeVarProvider::get_tag_vars(
        const PackageSpec& spec) const
    {
        auto find_itr = tag_vars.find(spec);
        if (find_itr != tag_vars.end())
        {
            return find_itr->second;
        }

        return nullopt;
    }

    Optional<const std::unordered_map<std::string, std::string>&> TripletCMakeVarProvider::get_triplet_vars(
        const PackageSpec& spec) const
    {
        auto find_itr = triplet_vars.find(spec);
        if (find_itr != triplet_vars.end())
        {
            return find_itr->second;
        }

        return nullopt;
    }
}
