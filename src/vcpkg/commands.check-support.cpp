#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.check-support.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/input.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/platform-expression.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    struct Port
    {
        std::string port_name;
        std::vector<std::string> features;
        Triplet triplet;
        std::string supports_expr;
    };

    Json::Object to_object(const Port& p)
    {
        Json::Object res;
        res.insert(JsonIdName, Json::Value::string(p.port_name));
        res.insert(JsonIdTriplet, Json::Value::string(p.triplet.to_string()));

        Json::Array& features = res.insert(JsonIdFeatures, Json::Array{});
        for (const auto& feature : p.features)
        {
            features.push_back(Json::Value::string(feature));
        }

        if (!p.supports_expr.empty())
        {
            res.insert(JsonIdSupports, Json::Value::string(p.supports_expr));
        }

        return res;
    }

    void print_port_supported(const Port& p, bool is_top_level_supported, View<Port> reasons)
    {
        const auto full_port_name = [](const Port& port) {
            return fmt::format(
                "{}[{}]:{}", port.port_name, Strings::join(",", port.features), port.triplet.to_string());
        };

        if (reasons.size() == 0)
        {
            if (is_top_level_supported)
            {
                // supported!
                msg::println(msgSupportedPort, msg::package_name = full_port_name(p));
            }
            else
            {
                msg::println(msg::format(msgUnsupportedPort, msg::package_name = full_port_name(p))
                                 .append_raw('\n')
                                 .append(msgPortSupportsField, msg::supports_expression = p.supports_expr));
            }

            return;
        }
        auto message = msg::format(msgUnsupportedPort, msg::package_name = full_port_name(p)).append_raw('\n');
        if (is_top_level_supported)
        {
            message.append(msgPortDependencyConflict, msg::package_name = full_port_name(p));
        }
        else
        {
            message.append(msgPortSupportsField, msg::supports_expression = p.supports_expr)
                .append_raw('\n')
                .append(msgPortDependencyConflict, msg::package_name = full_port_name(p));
        }

        for (const Port& reason : reasons)
        {
            message.append_raw('\n')
                .append_indent()
                .append(msgUnsupportedPortDependency, msg::value = full_port_name(p))
                .append(msgPortSupportsField, msg::supports_expression = reason.supports_expr);
        }
        msg::println(message);
    }

    constexpr CommandSwitch CHECK_SUPPORT_SWITCHES[] = {
        {SwitchXJson, msgJsonSwitch},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandCheckSupportMetadata{
        "x-check-support",
        msgCmdCheckSupportSynopsis,
        {msgCmdCheckSupportExample1, "vcpkg x-check-support zlib"},
        Undocumented,
        AutocompletePriority::Public,
        1,
        SIZE_MAX,
        {CHECK_SUPPORT_SWITCHES},
        nullptr,
    };

    void command_check_support_and_exit(const VcpkgCmdArguments& args,
                                        const VcpkgPaths& paths,
                                        Triplet default_triplet,
                                        Triplet host_triplet)
    {
        msg::default_output_stream = OutputStream::StdErr;
        const ParsedArguments options = args.parse_arguments(CommandCheckSupportMetadata);
        const bool use_json = Util::Sets::contains(options.switches, SwitchXJson);

        Json::Array json_to_print; // only used when `use_json`

        const std::vector<FullPackageSpec> specs = Util::fmap(options.command_arguments, [&](const std::string& arg) {
            return check_and_get_full_package_spec(arg, default_triplet, paths.get_triplet_db())
                .value_or_exit(VCPKG_LINE_INFO);
        });

        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set, make_overlay_provider(fs, paths.overlay_ports));
        auto cmake_vars = CMakeVars::make_triplet_cmake_var_provider(paths);

        // for each spec in the user-requested specs, check all dependencies
        CreateInstallPlanOptions create_options{
            nullptr, host_triplet, UnsupportedPortAction::Error, UseHeadVersion::No, Editable::No};
        PackagesDirAssigner packages_dir_assigner{paths.packages()};
        for (const auto& user_spec : specs)
        {
            auto action_plan = create_feature_install_plan(
                provider, *cmake_vars, {&user_spec, 1}, {}, packages_dir_assigner, create_options);

            cmake_vars->load_tag_vars(action_plan, host_triplet);

            Port user_port;
            user_port.port_name = user_spec.package_spec.name();
            user_port.triplet = user_spec.package_spec.triplet();
            bool user_supported = false;

            std::vector<Port> dependencies_not_supported;
            for (const auto& action : action_plan.install_actions)
            {
                const auto& spec = action.spec;
                const auto& supports_expression = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO)
                                                      .source_control_file->core_paragraph->supports_expression;

                PlatformExpression::Context context = cmake_vars->get_tag_vars(spec).value_or_exit(VCPKG_LINE_INFO);

                if (spec.name() == user_port.port_name && spec.triplet() == user_port.triplet)
                {
                    user_port.features = action.feature_list;
                    user_port.supports_expr = to_string(supports_expression);

                    if (supports_expression.evaluate(context))
                    {
                        user_supported = true;
                    }

                    continue;
                }

                if (!supports_expression.evaluate(context))
                {
                    Port port;
                    port.port_name = spec.name();
                    port.features = action.feature_list;
                    port.triplet = spec.triplet();
                    port.supports_expr = to_string(supports_expression);

                    dependencies_not_supported.push_back(port);
                }
            }

            if (use_json)
            {
                Json::Object& obj = json_to_print.push_back(Json::Object{});
                obj.insert("port", to_object(user_port));
                obj.insert("top-level-support", Json::Value::boolean(user_supported));
                obj.insert("is-supported", Json::Value::boolean(user_supported && dependencies_not_supported.empty()));
                if (!dependencies_not_supported.empty())
                {
                    Json::Array& deps = obj.insert("dependencies-not-supported", Json::Array{});
                    for (const Port& p : dependencies_not_supported)
                    {
                        deps.push_back(to_object(p));
                    }
                }
            }
            else
            {
                print_port_supported(user_port, user_supported, dependencies_not_supported);
            }
        }

        if (use_json)
        {
            msg::write_unlocalized_text(Color::none, Json::stringify(json_to_print));
        }
    }
} // namespace vcpkg
