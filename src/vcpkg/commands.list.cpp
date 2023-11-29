#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.list.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral OPTION_FULLDESC = "x-full-desc";
    constexpr StringLiteral OPTION_JSON = "x-json";

    void do_print_json(std::vector<const vcpkg::StatusParagraph*> installed_packages)
    {
        Json::Object obj;
        for (const StatusParagraph* status_paragraph : installed_packages)
        {
            auto current_spec = status_paragraph->package.spec;
            if (obj.contains(current_spec.to_string()))
            {
                if (status_paragraph->package.is_feature())
                {
                    Json::Value* value_obj = obj.get(current_spec.to_string());
                    auto& feature_list = value_obj->object(VCPKG_LINE_INFO)["features"].array(VCPKG_LINE_INFO);
                    feature_list.push_back(Json::Value::string(status_paragraph->package.feature));
                }
            }
            else
            {
                Json::Object& library_obj = obj.insert(current_spec.to_string(), Json::Object());
                library_obj.insert("package_name", Json::Value::string(current_spec.name()));
                library_obj.insert("triplet", Json::Value::string(current_spec.triplet().to_string()));
                library_obj.insert("version", Json::Value::string(status_paragraph->package.version.text));
                library_obj.insert("port_version",
                                   Json::Value::integer(status_paragraph->package.version.port_version));
                Json::Array& features_array = library_obj.insert("features", Json::Array());
                if (status_paragraph->package.is_feature())
                {
                    features_array.push_back(Json::Value::string(status_paragraph->package.feature));
                }
                Json::Array& desc = library_obj.insert("desc", Json::Array());
                for (const auto& line : status_paragraph->package.description)
                {
                    desc.push_back(Json::Value::string(line));
                }
            }
        }

        msg::write_unlocalized_text_to_stdout(Color::none, Json::stringify(obj));
    }

    void do_print(const StatusParagraph& pgh, const bool full_desc)
    {
        auto full_version = pgh.package.version.to_string();
        if (full_desc)
        {
            msg::write_unlocalized_text_to_stdout(Color::none,
                                                  fmt::format("{:<50}{:<20}{:<}\n",
                                                              pgh.package.display_name(),
                                                              full_version,
                                                              fmt::join(pgh.package.description, "\n\n")));
        }
        else
        {
            std::string description;
            if (!pgh.package.description.empty())
            {
                description = pgh.package.description[0];
            }
            msg::write_unlocalized_text_to_stdout(Color::none,
                                                  fmt::format("{:<50}{:<20}{:<}\n",
                                                              vcpkg::shorten_text(pgh.package.display_name(), 50),
                                                              vcpkg::shorten_text(full_version, 16),
                                                              vcpkg::shorten_text(description, 51)));
        }
    }

    constexpr CommandSwitch LIST_SWITCHES[] = {
        {OPTION_FULLDESC, msgHelpTextOptFullDesc},
        {OPTION_JSON, msgJsonSwitch},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandListMetadata{
        "list",
        msgListHelp,
        {"vcpkg list", msgCmdListExample2, "vcpkg list png"},
        "https://learn.microsoft.com/vcpkg/commands/list",
        AutocompletePriority::Public,
        0,
        1,
        {LIST_SWITCHES},
        nullptr,
    };

    void command_list_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const ParsedArguments options = args.parse_arguments(CommandListMetadata);

        const StatusParagraphs status_paragraphs = database_load_check(paths.get_filesystem(), paths.installed());
        auto installed_ipv = get_installed_ports(status_paragraphs);

        const auto output_json = Util::Sets::contains(options.switches, OPTION_JSON);
        if (installed_ipv.empty())
        {
            if (output_json)
                msg::write_unlocalized_text_to_stdout(Color::none, Json::stringify(Json::Object()));
            else
                msg::println(msgNoInstalledPackages);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        auto installed_packages = Util::fmap(installed_ipv, [](const InstalledPackageView& ipv) { return ipv.core; });
        auto installed_features =
            Util::fmap_flatten(installed_ipv, [](const InstalledPackageView& ipv) { return ipv.features; });
        installed_packages.insert(installed_packages.end(), installed_features.begin(), installed_features.end());

        std::sort(installed_packages.begin(),
                  installed_packages.end(),
                  [](const StatusParagraph* lhs, const StatusParagraph* rhs) -> bool {
                      return lhs->package.display_name() < rhs->package.display_name();
                  });

        const auto enable_fulldesc = Util::Sets::contains(options.switches, OPTION_FULLDESC.to_string());

        if (!options.command_arguments.empty())
        {
            auto& query = options.command_arguments[0];
            auto pghs = Util::filter(installed_packages, [query](const StatusParagraph* status_paragraph) {
                return Strings::case_insensitive_ascii_contains(status_paragraph->package.display_name(), query);
            });
            installed_packages = pghs;
        }

        if (output_json)
        {
            do_print_json(installed_packages);
        }
        else
        {
            for (const StatusParagraph* status_paragraph : installed_packages)
            {
                do_print(*status_paragraph, enable_fulldesc);
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
