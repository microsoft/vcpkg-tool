#include <vcpkg/base/json.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/commands.install.h>
#include <vcpkg/commands.package-info.h>
#include <vcpkg/input.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/versions.h>

namespace vcpkg::Commands::PackageInfo
{
    static constexpr StringLiteral OPTION_JSON = "x-json";
    static constexpr StringLiteral OPTION_TRANSITIVE = "x-transitive";
    static constexpr StringLiteral OPTION_INSTALLED = "x-installed";

    static constexpr CommandSwitch INFO_SWITCHES[] = {
        {OPTION_JSON, []() { return msg::format(msgJsonSwitch); }},
        {OPTION_INSTALLED, []() { return msg::format(msgCmdInfoOptInstalled); }},
        {OPTION_TRANSITIVE, []() { return msg::format(msgCmdInfoOptTransitive); }},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        [] {
            return msg::format(msgPackageInfoHelp)
                .append_raw('\n')
                .append(create_example_string("x-package-info zlib openssl:x64-windows"));
        },
        1,
        SIZE_MAX,
        {INFO_SWITCHES, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);
        if (!Util::Vectors::contains(options.switches, OPTION_JSON))
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, msgMissingOption, msg::option = OPTION_JSON);
        }

        const bool installed = Util::Sets::contains(options.switches, OPTION_INSTALLED);
        const bool transitive = Util::Sets::contains(options.switches, OPTION_TRANSITIVE);

        if (transitive && !installed)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                          msgOptionRequiresOption,
                                          msg::value = OPTION_TRANSITIVE,
                                          msg::option = OPTION_INSTALLED);
        }

        auto& fs = paths.get_filesystem();
        if (installed)
        {
            const StatusParagraphs status_paragraphs = database_load_check(fs, paths.installed());
            std::set<PackageSpec> specs_written;
            std::vector<PackageSpec> specs_to_write;
            for (auto&& arg : options.command_arguments)
            {
                ParserBase parser(arg, "<command>");
                auto maybe_qpkg = parse_qualified_specifier(parser);
                if (!parser.at_eof() || !maybe_qpkg)
                {
                    parser.add_error(msg::format(msgExpectedPackageSpecifier));
                }
                else if (!maybe_qpkg.get()->triplet)
                {
                    parser.add_error(msg::format(msgExpectedExplicitTriplet));
                }
                else if (maybe_qpkg.get()->features)
                {
                    parser.add_error(msg::format(msgUnexpectedFeatureList));
                }
                else if (maybe_qpkg.get()->platform)
                {
                    parser.add_error(msg::format(msgUnexpectedPlatformExpression));
                }
                if (auto err = parser.get_error())
                {
                    Checks::exit_with_message(VCPKG_LINE_INFO, err->to_string());
                }

                auto& qpkg = *maybe_qpkg.get();
                // intentionally no triplet name check
                specs_to_write.emplace_back(qpkg.name, Triplet::from_canonical_name(*qpkg.triplet.get()));
            }

            Json::Object response;
            Json::Object results;
            while (!specs_to_write.empty())
            {
                auto spec = std::move(specs_to_write.back());
                specs_to_write.pop_back();
                if (!specs_written.insert(spec).second) continue;
                auto maybe_ipv = status_paragraphs.get_installed_package_view(spec);
                if (auto ipv = maybe_ipv.get())
                {
                    results.insert(spec.to_string(), serialize_ipv(*ipv, paths.installed(), paths.get_filesystem()));
                    if (transitive)
                    {
                        auto deps = ipv->dependencies();
                        specs_to_write.insert(specs_to_write.end(),
                                              std::make_move_iterator(deps.begin()),
                                              std::make_move_iterator(deps.end()));
                    }
                }
            }
            response.insert("results", std::move(results));
            msg::write_unlocalized_text_to_stdout(Color::none, Json::stringify(response));
        }
        else
        {
            Json::Object response;
            Json::Object results;
            auto registry_set = paths.make_registry_set();
            PathsPortFileProvider provider(
                fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));

            for (auto&& arg : options.command_arguments)
            {
                ParserBase parser(arg, "<command>");
                auto maybe_pkg = parse_package_name(parser);
                if (!parser.at_eof() || !maybe_pkg)
                {
                    parser.add_error(msg::format(msgExpectedPortName));
                }
                if (auto err = parser.get_error())
                {
                    Checks::exit_with_message(VCPKG_LINE_INFO, err->to_string());
                }

                auto& pkg = *maybe_pkg.get();

                if (results.contains(pkg)) continue;

                auto maybe_scfl = provider.get_control_file(pkg);

                Json::Object obj;
                if (auto pscfl = maybe_scfl.get())
                {
                    results.insert(pkg, serialize_manifest(*pscfl->source_control_file));
                }
            }
            response.insert("results", std::move(results));
            msg::write_unlocalized_text_to_stdout(Color::none, Json::stringify(response));
        }
    }
}
