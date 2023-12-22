#include <vcpkg/base/cmd-parser.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/commands.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    struct Topic
    {
        StringLiteral name;
        void (*print)(const VcpkgPaths&);
    };

    LocalizedString help_topics();

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
    "builtin-baseline": "a14a6bcb27287e3ec138dba1b948a0cdbc337a3a",
    "dependencies": [
        { "name": "zlib", "version>=": "1.2.11#8" },
        "rapidjson"
    ],
    "overrides": [
        { "name": "rapidjson", "version": "2020-09-14" }
    ]
})");
        msg::println(LocalizedString::from_raw(std::move(tbl).m_str));
        msg::println(msgExtendedDocumentationAtUrl, msg::url = docs::versioning_url);
    }

    constexpr Topic topics[] = {
        {"assetcaching", [](const VcpkgPaths&) { msg::println(format_help_topic_asset_caching()); }},
        {"binarycaching", [](const VcpkgPaths&) { msg::println(format_help_topic_binary_caching()); }},
        {"commands", [](const VcpkgPaths&) { print_full_command_list(); }},
        {"topics", [](const VcpkgPaths&) { msg::print(help_topics()); }},
        {"triplet", [](const VcpkgPaths& paths) { help_topic_valid_triplet(paths.get_triplet_db()); }},
        {"versioning", help_topic_versioning},
    };

    LocalizedString help_topics()
    {
        std::vector<LocalizedString> all_topic_names;
        for (auto&& topic : topics)
        {
            all_topic_names.push_back(LocalizedString::from_raw(topic.name));
        }

        for (auto&& command_metadata : get_all_commands_metadata())
        {
            all_topic_names.push_back(LocalizedString::from_raw(command_metadata->name));
        }

        Util::sort(all_topic_names);

        LocalizedString result;
        result.append(msgAvailableHelpTopics);
        result.append_floating_list(1, all_topic_names);
        result.append_raw('\n');
        return result;
    }

} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandHelpMetadata{
        "help",
        msgHelpTopicCommand,
        {"vcpkg help topics", "vcpkg help commands", "vcpkg help install"},
        Undocumented,
        AutocompletePriority::Public,
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

        LocalizedString result;
        result.append(msgBuiltInTriplets).append_raw('\n');
        for (auto* triplet : triplets_per_location[database.default_triplet_directory])
        {
            result.append_indent().append_raw(triplet->name).append_raw('\n');
        }

        triplets_per_location.erase(database.default_triplet_directory);
        result.append(msgCommunityTriplets).append_raw('\n');
        for (auto* triplet : triplets_per_location[database.community_triplet_directory])
        {
            result.append_indent().append_raw(triplet->name).append_raw('\n');
        }

        triplets_per_location.erase(database.community_triplet_directory);
        for (auto&& kv_pair : triplets_per_location)
        {
            result.append(msgOverlayTriplets, msg::path = kv_pair.first).append_raw('\n');
            for (auto* triplet : kv_pair.second)
            {
                result.append_indent().append_raw(triplet->name).append_raw('\n');
            }
        }

        result.append(msgSeeURL, msg::url = "https://learn.microsoft.com/vcpkg/users/triplets");
        result.append_raw('\n');
        msg::print(result);
    }

    void command_help_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const auto parsed = args.parse_arguments(CommandHelpMetadata);

        if (parsed.command_arguments.empty())
        {
            msg::write_unlocalized_text_to_stdout(Color::none, get_zero_args_usage());
            Checks::exit_success(VCPKG_LINE_INFO);
        }
        const auto& topic = parsed.command_arguments[0];
        if (Strings::case_insensitive_ascii_equals(topic, "triplets") ||
            Strings::case_insensitive_ascii_equals(topic, "triple"))
        {
            help_topic_valid_triplet(paths.get_triplet_db());
            get_global_metrics_collector().track_string(StringMetric::CommandContext, "triplet");
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        for (auto&& candidate : topics)
        {
            if (Strings::case_insensitive_ascii_equals(candidate.name, topic))
            {
                candidate.print(paths);
                get_global_metrics_collector().track_string(StringMetric::CommandContext, candidate.name);
                Checks::exit_success(VCPKG_LINE_INFO);
            }
        }

        for (auto&& command_metadata : get_all_commands_metadata())
        {
            if (Strings::case_insensitive_ascii_equals(command_metadata->name, topic))
            {
                msg::print(usage_for_command(*command_metadata));
                get_global_metrics_collector().track_string(StringMetric::CommandContext, command_metadata->name);
                Checks::exit_success(VCPKG_LINE_INFO);
            }
        }

        stderr_sink.println_error(msgUnknownTopic, msg::value = topic);
        stderr_sink.print(help_topics());
        get_global_metrics_collector().track_string(StringMetric::CommandContext, "unknown");
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
