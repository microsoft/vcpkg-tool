#include <vcpkg/base/system.print.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/commands.create.h>
#include <vcpkg/commands.dependinfo.h>
#include <vcpkg/commands.edit.h>
#include <vcpkg/commands.env.h>
#include <vcpkg/commands.integrate.h>
#include <vcpkg/commands.list.h>
#include <vcpkg/commands.owns.h>
#include <vcpkg/commands.search.h>
#include <vcpkg/documentation.h>
#include <vcpkg/export.h>
#include <vcpkg/help.h>
#include <vcpkg/install.h>
#include <vcpkg/remove.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Help
{
    struct Topic
    {
        using topic_function = void (*)(const VcpkgPaths& paths);

        constexpr Topic(StringLiteral n, topic_function fn) : name(n), print(fn) { }

        StringLiteral name;
        topic_function print;
    };

    template<const CommandStructure& S>
    static void command_topic_fn(const VcpkgPaths&)
    {
        print_usage(S);
    }

    static void integrate_topic_fn(const VcpkgPaths&)
    {
        msg::write_unlocalized_text_to_stdout(Color::none, "Commands:\n" + Commands::Integrate::get_helpstring());
    }

    static void help_topics(const VcpkgPaths&);

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("help"),
        0,
        1,
        {},
        nullptr,
    };

    static void help_topic_versioning(const VcpkgPaths&)
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

    static constexpr std::array<Topic, 17> topics = {{
        {"binarycaching", help_topic_binary_caching},
        {"assetcaching", help_topic_asset_caching},
        {"create", command_topic_fn<Commands::Create::COMMAND_STRUCTURE>},
        {"depend-info", command_topic_fn<Commands::DependInfo::COMMAND_STRUCTURE>},
        {"edit", command_topic_fn<Commands::Edit::COMMAND_STRUCTURE>},
        {"env", command_topic_fn<Commands::Env::COMMAND_STRUCTURE>},
        {"export", command_topic_fn<Export::COMMAND_STRUCTURE>},
        {"help", command_topic_fn<Help::COMMAND_STRUCTURE>},
        {"install", command_topic_fn<Install::COMMAND_STRUCTURE>},
        {"integrate", integrate_topic_fn},
        {"list", command_topic_fn<Commands::List::COMMAND_STRUCTURE>},
        {"owns", command_topic_fn<Commands::Owns::COMMAND_STRUCTURE>},
        {"remove", command_topic_fn<Remove::COMMAND_STRUCTURE>},
        {"search", command_topic_fn<Commands::SearchCommandStructure>},
        {"topics", help_topics},
        {"triplet", help_topic_valid_triplet},
        {"versioning", help_topic_versioning},
    }};

    static void help_topics(const VcpkgPaths&)
    {
        auto msg = msg::format(msgAvailableHelpTopics);
        for (auto topic : topics)
        {
            msg.append_raw(fmt::format("\n  {}", topic.name));
        }
        msg::println(msg);
    }

    void help_topic_valid_triplet(const VcpkgPaths& paths)
    {
        std::map<StringView, std::vector<const VcpkgPaths::TripletFile*>> triplets_per_location;
        vcpkg::Util::group_by(
            paths.get_available_triplets(),
            &triplets_per_location,
            [](const VcpkgPaths::TripletFile& triplet_file) -> StringView { return triplet_file.location; });

        msg::println(msgAvailableArchitectureTriplets);
        msg::println(msgBuiltInTriplets);
        for (auto* triplet : triplets_per_location[paths.triplets])
        {
            msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("  {}\n", triplet->name));
        }

        triplets_per_location.erase(paths.triplets);
        msg::println(msgCommunityTriplets);
        for (auto* triplet : triplets_per_location[paths.community_triplets])
        {
            msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("  {}\n", triplet->name));
        }

        triplets_per_location.erase(paths.community_triplets);
        for (auto&& kv_pair : triplets_per_location)
        {
            msg::println(msgOverlayTriplets, msg::path = kv_pair.first);
            for (auto* triplet : kv_pair.second)
            {
                msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("  {}\n", triplet->name));
            }
        }
    }

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        (void)args.parse_arguments(COMMAND_STRUCTURE);

        if (args.command_arguments.empty())
        {
            print_usage();
            Checks::exit_success(VCPKG_LINE_INFO);
        }
        const auto& topic = args.command_arguments[0];
        if (topic == "triplets" || topic == "triple")
        {
            help_topic_valid_triplet(paths);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        auto it_topic = Util::find_if(topics, [&](const Topic& t) { return t.name == topic; });
        if (it_topic != topics.end())
        {
            it_topic->print(paths);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        msg::println_error(msgUnknownTopic, msg::value = topic);
        help_topics(paths);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    void HelpCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Help::perform_and_exit(args, paths);
    }
}
