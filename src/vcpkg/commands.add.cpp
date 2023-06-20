#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.add.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    const CommandStructure AddCommandStructure = {
        [] {
            return msg::format(msgAddHelp)
                .append_raw('\n')
                .append(create_example_string("add port png"))
                .append_raw('\n')
                .append(create_example_string("add artifact cmake"));
        },
        2,
        SIZE_MAX,
        {{}, {}},
        nullptr,
    };
}

namespace vcpkg::Commands
{
    void command_add_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        MetricsSubmission metrics;
        auto parsed = args.parse_arguments(AddCommandStructure);
        auto&& selector = parsed.command_arguments[0];

        if (selector == "artifact")
        {
            Checks::msg_check_exit(VCPKG_LINE_INFO,
                                   parsed.command_arguments.size() <= 2,
                                   msgAddArtifactOnlyOne,
                                   msg::command_line = "vcpkg add artifact");

            auto& artifact_name = parsed.command_arguments[1];
            auto artifact_hash = Hash::get_string_hash(artifact_name, Hash::Algorithm::Sha256);
            metrics.track_string(StringMetric::CommandContext, "artifact");
            metrics.track_string(StringMetric::CommandArgs, artifact_hash);
            get_global_metrics_collector().track_submission(std::move(metrics));

            std::string ce_args[] = {"add", artifact_name};
            Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, ce_args));
        }

        if (selector == "port")
        {
            auto manifest = paths.get_manifest().get();
            if (!manifest)
            {
                Checks::msg_exit_with_message(
                    VCPKG_LINE_INFO, msgAddPortRequiresManifest, msg::command_line = "vcpkg add port");
            }

            std::vector<ParsedQualifiedSpecifier> specs;
            specs.reserve(parsed.command_arguments.size() - 1);
            for (std::size_t idx = 1; idx < parsed.command_arguments.size(); ++idx)
            {
                ParsedQualifiedSpecifier value =
                    parse_qualified_specifier(parsed.command_arguments[idx]).value_or_exit(VCPKG_LINE_INFO);
                if (const auto t = value.triplet.get())
                {
                    Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                                msgAddTripletExpressionNotAllowed,
                                                msg::package_name = value.name,
                                                msg::triplet = *t);
                }

                specs.push_back(std::move(value));
            }

            auto maybe_manifest_scf =
                SourceControlFile::parse_project_manifest_object(manifest->path, manifest->manifest, stdout_sink);
            if (!maybe_manifest_scf)
            {
                print_error_message(maybe_manifest_scf.error());
                msg::println(Color::error, msg::msgSeeURL, msg::url = docs::manifests_url);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            auto& manifest_scf = *maybe_manifest_scf.value(VCPKG_LINE_INFO);
            for (const auto& spec : specs)
            {
                auto dep = Util::find_if(manifest_scf.core_paragraph->dependencies, [&spec](Dependency& dep) {
                    return dep.name == spec.name && !dep.host &&
                           structurally_equal(spec.platform.value_or(PlatformExpression::Expr()), dep.platform);
                });
                if (dep == manifest_scf.core_paragraph->dependencies.end())
                {
                    manifest_scf.core_paragraph->dependencies.push_back(
                        Dependency{spec.name, spec.features.value_or({}), spec.platform.value_or({})});
                }
                else if (spec.features)
                {
                    for (const auto& feature : spec.features.value_or_exit(VCPKG_LINE_INFO))
                    {
                        if (!Util::Vectors::contains(dep->features, feature))
                        {
                            dep->features.push_back(feature);
                        }
                    }
                }
            }

            paths.get_filesystem().write_contents(
                manifest->path, Json::stringify(serialize_manifest(manifest_scf)), VCPKG_LINE_INFO);
            msg::println(msgAddPortSucceeded);

            auto command_args_hash = Strings::join(" ", Util::fmap(specs, [](auto&& spec) -> std::string {
                                                       return Hash::get_string_hash(spec.name, Hash::Algorithm::Sha256);
                                                   }));
            metrics.track_string(StringMetric::CommandContext, "port");
            metrics.track_string(StringMetric::CommandArgs, command_args_hash);
            get_global_metrics_collector().track_submission(std::move(metrics));

            Checks::exit_success(VCPKG_LINE_INFO);
        }

        Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgAddFirstArgument, msg::command_line = "vcpkg add");
    }
}
