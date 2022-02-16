#include <vcpkg/base/basic_checks.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.add.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/documentation.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    const CommandStructure AddCommandStructure = {
        Strings::format(
            "Adds the indicated port or artifact to the manifest associated with the current directory.\n%s\n%s",
            create_example_string("add port png"),
            create_example_string("add artifact cmake")),
        2,
        SIZE_MAX,
        {{}, {}},
        nullptr,
    };

    DECLARE_AND_REGISTER_MESSAGE(AddTripletExpressionNotAllowed,
                                 (),
                                 "Example for {name} is 'zlib', {triplet} is 'x64-windows'",
                                 "Error: triplet expressions are not allowed here. You may want to change "
                                 "`{name}:{triplet}` to `{name}` instead.");

    DECLARE_AND_REGISTER_MESSAGE(AddPortSucceded, (), "", "Succeeded in adding ports to vcpkg.json file.");
}

namespace vcpkg::Commands
{
    void AddCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        args.parse_arguments(AddCommandStructure);
        auto&& selector = args.command_arguments[0];
        if (selector == "artifact")
        {
            Checks::check_exit(
                VCPKG_LINE_INFO, args.command_arguments.size() > 2, "You can only add one artifact at the time.\n");
            std::string ce_args[] = {"add", args.command_arguments[1]};
            Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, ce_args));
        }

        if (selector == "port")
        {
            auto manifest = paths.get_manifest().get();
            if (!manifest)
            {
                Checks::exit_with_message(VCPKG_LINE_INFO, "add port requires an active manifest file.\n");
            }
            const std::vector<ParsedQualifiedSpecifier> specs = Util::fmap(args.command_arguments, [&](auto&& arg) {
                ExpectedS<ParsedQualifiedSpecifier> value = parse_qualified_specifier(std::string(arg));
                if (auto v = value.get())
                {
                    if (v->triplet)
                    {
                        msg::println(Color::error, msgAddTripletExpressionNotAllowed);
                    }
                    else
                    {
                        return std::move(*v);
                    }
                }
                else
                {
                    print2(Color::error, value.error());
                }
                Checks::exit_fail(VCPKG_LINE_INFO);
            });
            const auto& manifest_path = paths.get_manifest_path().value_or_exit(VCPKG_LINE_INFO);
            auto maybe_manifest_scf = SourceControlFile::parse_manifest_object(manifest_path, *manifest);
            if (!maybe_manifest_scf)
            {
                print_error_message(maybe_manifest_scf.error());
                msg::println(Color::error, msg::msgSeeURL, msg::url = docs::manifests_url);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            auto& manifest_scf = *maybe_manifest_scf.value_or_exit(VCPKG_LINE_INFO);
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
            std::error_code ec;
            paths.get_filesystem().write_contents(
                manifest_path, Json::stringify(serialize_manifest(manifest_scf), {}), ec);
            if (ec)
            {
                Checks::exit_with_message(
                    VCPKG_LINE_INFO, "Failed to write manifest file %s: %s\n", manifest_path, ec.message());
            }
            msg::println(msgAddPortSucceded);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        Checks::exit_with_message(VCPKG_LINE_INFO, "The first parmaeter to add must be 'artifact' or 'port'.\n");
    }
}
