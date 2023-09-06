#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/commands.acquire-project.h>
#include <vcpkg/commands.acquire.h>
#include <vcpkg/commands.activate.h>
#include <vcpkg/commands.add-version.h>
#include <vcpkg/commands.add.h>
#include <vcpkg/commands.build-external.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.check-support.h>
#include <vcpkg/commands.ci-clean.h>
#include <vcpkg/commands.ci-verify-versions.h>
#include <vcpkg/commands.ci.h>
#include <vcpkg/commands.contact.h>
#include <vcpkg/commands.create.h>
#include <vcpkg/commands.deactivate.h>
#include <vcpkg/commands.dependinfo.h>
#include <vcpkg/commands.download.h>
#include <vcpkg/commands.edit.h>
#include <vcpkg/commands.env.h>
#include <vcpkg/commands.export.h>
#include <vcpkg/commands.fetch.h>
#include <vcpkg/commands.find.h>
#include <vcpkg/commands.format-manifest.h>
#include <vcpkg/commands.hash.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/commands.init-registry.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/commands.integrate.h>
#include <vcpkg/commands.list.h>
#include <vcpkg/commands.new.h>
#include <vcpkg/commands.owns.h>
#include <vcpkg/commands.package-info.h>
#include <vcpkg/commands.portsdiff.h>
#include <vcpkg/commands.regenerate.h>
#include <vcpkg/commands.remove.h>
#include <vcpkg/commands.search.h>
#include <vcpkg/commands.set-installed.h>
#include <vcpkg/commands.update-baseline.h>
#include <vcpkg/commands.update-registry.h>
#include <vcpkg/commands.update.h>
#include <vcpkg/commands.upgrade.h>
#include <vcpkg/commands.use.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/commands.vsinstances.h>
#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    struct Topic
    {
        StringLiteral name;
        void (*print)(const VcpkgPaths&);
    };

    template<const CommandMetadata& S>
    void command_topic_fn(const VcpkgPaths&)
    {
        print_usage(S);
    }

    void integrate_topic_fn(const VcpkgPaths&) { msg::println(get_integrate_helpstring()); }

    void help_topics(const VcpkgPaths&);

    void help_topic_versioning(const VcpkgPaths&)
    {
        HelpTableFormatter tbl;
        tbl.text(msg::format(msgHelpVersioning));
        tbl.blank();
        tbl.blank();
        tbl.header(msg::format(msgHelpVersionSchemes));
        tbl.format("version", msg::format(msgHelpVersionScheme));
        tbl.format("version-date", msg::format(msgHelpVersionDateScheme));
        tbl.format("version-semver", msg::format(msgHelpVersionSemverScheme));
        tbl.format("version-string", msg::format(msgHelpVersionStringScheme));
        tbl.blank();
        tbl.text(msg::format(msgHelpPortVersionScheme));
        tbl.blank();
        tbl.blank();
        tbl.header(msg::format(msgHelpManifestConstraints));
        tbl.format("builtin-baseline", msg::format(msgHelpBuiltinBase));
        tbl.blank();
        tbl.format("version>=", msg::format(msgHelpVersionGreater));
        tbl.blank();
        tbl.format("overrides", msg::format(msgHelpOverrides));
        tbl.blank();
        tbl.text(msg::format(msgHelpMinVersion));
        tbl.blank();
        tbl.text(msg::format(msgHelpUpdateBaseline));
        tbl.blank();
        tbl.text(msg::format(msgHelpPackagePublisher));
        tbl.blank();
        tbl.text(msg::format(msgHelpExampleManifest));
        tbl.blank();
        tbl.text(R"({
    "name": "example",
    "version": "1.0",
    "builtin-baseline": "a14a6bcb27287e3ec138dba1b948a0cdbc337a3a",
    "dependencies": [
        { "name": "zlib", "version>=": "1.2.11#8" },
        "rapidjson"
    ],
    "overrides": [
        { "name": "rapidjson", "version": "2020-09-14" }
    ]
})");
        msg::write_unlocalized_text_to_stdout(Color::none, tbl.m_str);
        msg::println(msgExtendedDocumentationAtUrl, msg::url = docs::versioning_url);
    }

    constexpr Topic topics[] = {
        {"acquire", command_topic_fn<CommandAcquireMetadata>},
        {"acquire-project", command_topic_fn<CommandAcquireProjectMetadata>},
        {"activate", command_topic_fn<CommandActivateMetadata>},
        {"add", command_topic_fn<CommandAddMetadata>},
        {"x-add-version", command_topic_fn<CommandAddVersionMetadata>},
        {"assetcaching", [](const VcpkgPaths&) { msg::println(format_help_topic_asset_caching()); }},
        // autocomplete intentionally has no help topic
        {"binarycaching", [](const VcpkgPaths&) { msg::println(format_help_topic_binary_caching()); }},
        // bootstrap-standalone intentionally has no help topic
        {"build", command_topic_fn<CommandBuildMetadata>},
        {"build-external", command_topic_fn<CommandBuildExternalMetadata>},
        {"x-check-support", command_topic_fn<CommandCheckSupportMetadata>},
        {"ci", command_topic_fn<CommandCiMetadata>},
        {"x-ci-clean", command_topic_fn<CommandCiCleanMetadata>},
        {"x-ci-verify-versions", command_topic_fn<CommandCiVerifyVersionsMetadata>},
        {"create", command_topic_fn<CommandCreateMetadata>},
        {"contact", command_topic_fn<CommandContactMetadata>},
        {"deactivate", command_topic_fn<CommandDeactivateMetadata>},
        {"depend-info", command_topic_fn<CommandDependinfoMetadata>},
        {"x-download", command_topic_fn<CommandDownloadMetadata>},
        {"edit", command_topic_fn<CommandEditMetadata>},
        {"env", command_topic_fn<CommandEnvMetadata>},
        {"export", command_topic_fn<CommandExportMetadata>},
        {"fetch", command_topic_fn<CommandFetchMetadata>},
        {"find", command_topic_fn<CommandFindMetadata>},
        {"format-manifest", command_topic_fn<CommandFormatManifestMetadata>},
        {"hash", command_topic_fn<CommandHashMetadata>},
        {"help", command_topic_fn<CommandHelpMetadata>},
        {"x-init-registry", command_topic_fn<CommandInitRegistryMetadata>},
        {"install", command_topic_fn<CommandInstallMetadata>},
        {"integrate", integrate_topic_fn},
        {"list", command_topic_fn<CommandListMetadata>},
        {"new", command_topic_fn<CommandNewMetadata>},
        {"owns", command_topic_fn<CommandOwnsMetadata>},
        {"x-package-info", command_topic_fn<CommandPackageInfoMetadata>},
        {"portsdiff", command_topic_fn<CommandPortsdiffMetadata>},
        {"x-regenerate", command_topic_fn<CommandRegenerateMetadata>},
        {"remove", command_topic_fn<CommandRemoveMetadata>},
        {"search", command_topic_fn<CommandSearchMetadata>},
        {"x-set-installed", command_topic_fn<CommandSetInstalledMetadata>},
        {"topics", help_topics},
        {"triplet", [](const VcpkgPaths& paths) { help_topic_valid_triplet(paths.get_triplet_db()); }},
        {"x-update-baseline", command_topic_fn<CommandUpdateBaselineMetadata>},
        {"x-update-registry", command_topic_fn<CommandUpdateRegistryMetadata>},
        {"update", command_topic_fn<CommandUpdateMetadata>},
        {"upgrade", command_topic_fn<CommandUpgradeMetadata>},
        {"use", command_topic_fn<CommandUseMetadata>},
        {"version", command_topic_fn<CommandVersionMetadata>},
        {"versioning", help_topic_versioning},
        {"x-vs-instances", command_topic_fn<CommandVsInstancesMetadata>},
    };

    void help_topics(const VcpkgPaths&)
    {
        auto msg = msg::format(msgAvailableHelpTopics);
        for (auto&& topic : topics)
        {
            msg.append_raw(fmt::format("\n  {}", topic.name));
        }
        msg::println(msg);
    }

} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandHelpMetadata = {
        [] { return create_example_string("help"); },
        0,
        1,
        {},
        nullptr,
    };

    void help_topic_valid_triplet(const TripletDatabase& database)
    {
        std::map<StringView, std::vector<const TripletFile*>> triplets_per_location;
        vcpkg::Util::group_by(database.available_triplets,
                              &triplets_per_location,
                              [](const TripletFile& triplet_file) -> StringView { return triplet_file.location; });

        msg::println(msgAvailableArchitectureTriplets);
        msg::println(msgBuiltInTriplets);
        for (auto* triplet : triplets_per_location[database.default_triplet_directory])
        {
            msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("  {}\n", triplet->name));
        }

        triplets_per_location.erase(database.default_triplet_directory);
        msg::println(msgCommunityTriplets);
        for (auto* triplet : triplets_per_location[database.community_triplet_directory])
        {
            msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("  {}\n", triplet->name));
        }

        triplets_per_location.erase(database.community_triplet_directory);
        for (auto&& kv_pair : triplets_per_location)
        {
            msg::println(msgOverlayTriplets, msg::path = kv_pair.first);
            for (auto* triplet : kv_pair.second)
            {
                msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("  {}\n", triplet->name));
            }
        }
    }

    void command_help_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const auto parsed = args.parse_arguments(CommandHelpMetadata);

        if (parsed.command_arguments.empty())
        {
            print_command_list_usage();
            Checks::exit_success(VCPKG_LINE_INFO);
        }
        const auto& topic = parsed.command_arguments[0];
        if (topic == "triplets" || topic == "triple")
        {
            help_topic_valid_triplet(paths.get_triplet_db());
            get_global_metrics_collector().track_string(StringMetric::CommandContext, topic);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        for (auto&& candidate : topics)
        {
            if (candidate.name == topic)
            {
                candidate.print(paths);
                get_global_metrics_collector().track_string(StringMetric::CommandContext, candidate.name);
                Checks::exit_success(VCPKG_LINE_INFO);
            }
        }

        msg::println_error(msgUnknownTopic, msg::value = topic);
        help_topics(paths);
        get_global_metrics_collector().track_string(StringMetric::CommandContext, "unknown");
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
