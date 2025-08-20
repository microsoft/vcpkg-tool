#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.package-info.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    constexpr CommandSwitch INFO_SWITCHES[] = {
        {SwitchXJson, msgJsonSwitch},
        {SwitchXInstalled, msgCmdInfoOptInstalled},
        {SwitchXTransitive, msgCmdInfoOptTransitive},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandPackageInfoMetadata{
        "x-package-info",
        msgPackageInfoHelp,
        {msgCmdPackageInfoExample1, "vcpkg x-package-info zlib openssl:x64-windows"},
        Undocumented,
        AutocompletePriority::Public,
        1,
        SIZE_MAX,
        {INFO_SWITCHES},
        nullptr,
    };

    void command_package_info_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        msg::default_output_stream = OutputStream::StdErr;
        const ParsedArguments options = args.parse_arguments(CommandPackageInfoMetadata);
        if (!Util::Vectors::contains(options.switches, SwitchXJson))
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, msgMissingOption, msg::option = SwitchXJson);
        }

        const bool installed = Util::Sets::contains(options.switches, SwitchXInstalled);
        const bool transitive = Util::Sets::contains(options.switches, SwitchXTransitive);

        if (transitive && !installed)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                          msgOptionRequiresOption,
                                          msg::value = SwitchXTransitive,
                                          msg::option = SwitchXInstalled);
        }

        auto& fs = paths.get_filesystem();
        if (installed)
        {
            const StatusParagraphs status_paragraphs = database_load(fs, paths.installed());
            std::set<PackageSpec> specs_written;
            std::vector<PackageSpec> specs_to_write;
            for (auto&& arg : options.command_arguments)
            {
                auto qpkg = parse_qualified_specifier(
                                arg, AllowFeatures::No, ParseExplicitTriplet::Require, AllowPlatformSpec::No)
                                .value_or_exit(VCPKG_LINE_INFO);
                // intentionally no triplet name check
                specs_to_write.emplace_back(
                    qpkg.name.value, Triplet::from_canonical_name(qpkg.triplet.value_or_exit(VCPKG_LINE_INFO).value));
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
            PathsPortFileProvider provider(*registry_set, make_overlay_provider(fs, paths.overlay_ports));

            for (auto&& arg : options.command_arguments)
            {
                ParserBase parser(arg, nullopt, {0, 0});
                auto maybe_pkg = parse_package_name(parser);
                if (!parser.at_eof() || !maybe_pkg)
                {
                    parser.add_error(msg::format(msgExpectedPortName));
                }
                if (parser.messages().any_errors())
                {
                    parser.messages().exit_if_errors_or_warnings();
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
} // namespace vcpkg
