#pragma once

#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/cmd-parser.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>

#include <assert.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace vcpkg
{
    struct ParsedArguments
    {
        std::set<StringLiteral, std::less<>> switches;
        std::map<StringLiteral, std::string, std::less<>> settings;
        std::map<StringLiteral, std::vector<std::string>, std::less<>> multisettings;

        const std::string* read_setting(StringLiteral setting) const noexcept;

        std::vector<std::string> command_arguments;
    };

    struct MetadataMessage
    {
        constexpr MetadataMessage() noexcept : kind(MetadataMessageKind::Unused), literal{} { }
        /*implicit*/ constexpr MetadataMessage(const msg::MessageT<>& message) noexcept
            : kind(MetadataMessageKind::Message), message(&message)
        {
        }

        template<int N>
        /*implicit*/ constexpr MetadataMessage(const char (&literal)[N]) noexcept
            : kind(MetadataMessageKind::Literal), literal(literal)
        {
        }

        template<class Callback,
                 std::enable_if_t<std::is_convertible_v<const Callback&, LocalizedString (*)()>, int> = 0>
        /*implicit*/ constexpr MetadataMessage(const Callback& callback) noexcept
            : kind(MetadataMessageKind::Callback), callback(callback)
        {
        }

        constexpr MetadataMessage(std::nullptr_t) = delete;

        MetadataMessage(const MetadataMessage&) = delete;
        MetadataMessage& operator=(const MetadataMessage&) = delete;

        LocalizedString to_string() const;
        void to_string(LocalizedString& target) const;
        explicit operator bool() const noexcept;

    private:
        enum class MetadataMessageKind
        {
            Unused,
            Message,
            Literal,
            Callback
        };

        MetadataMessageKind kind;
        union
        {
            const msg::MessageT<>* message;
            const char* literal; // not StringLiteral so this union is sizeof(void*)
            LocalizedString (*callback)();
        };
    };

    inline constexpr bool constexpr_contains(const char* haystack, const char* needle) noexcept
    {
        for (;; ++haystack)
        {
            for (std::size_t offset = 0;; ++offset)
            {
                if (!needle[offset])
                {
                    return true;
                }

                if (!haystack[offset])
                {
                    return false;
                }

                if (needle[offset] != haystack[offset])
                {
                    break;
                }
            }
        }
    }

    static_assert(constexpr_contains("", ""), "boom");
    static_assert(constexpr_contains("hay", ""), "boom");
    static_assert(!constexpr_contains("", "needle"), "boom");
    static_assert(constexpr_contains("needle", "nee"), "boom");

    struct LearnWebsiteLinkLiteralUndocumentedCookie
    {
    };

    constexpr LearnWebsiteLinkLiteralUndocumentedCookie Undocumented;

    struct LearnWebsiteLinkLiteral
    {
        /*implicit*/ constexpr LearnWebsiteLinkLiteral(LearnWebsiteLinkLiteralUndocumentedCookie) noexcept : literal{}
        {
        }

        template<int N>
        /*implicit*/ constexpr LearnWebsiteLinkLiteral(const char (&literal)[N]) noexcept : literal(literal)
        {
            assert(!constexpr_contains(literal, "en-us") &&
                   "If you get a build error here, remove the en-us from the learn uri so that the correct locale is "
                   "chosen for the user");
        }

        LocalizedString to_string() const;
        void to_string(LocalizedString& target) const;
        explicit operator bool() const noexcept;

    private:
        const char* literal; // not StringLiteral to be nullable
    };

    struct CommandSwitch
    {
        StringLiteral name;
        MetadataMessage helpmsg;
    };

    struct CommandSetting
    {
        StringLiteral name;
        MetadataMessage helpmsg;
    };

    struct CommandMultiSetting
    {
        StringLiteral name;
        MetadataMessage helpmsg;
    };

    struct CommandOptionsStructure
    {
        View<CommandSwitch> switches;
        View<CommandSetting> settings;
        View<CommandMultiSetting> multisettings;
    };

    struct CommandMetadata
    {
        StringLiteral name;
        MetadataMessage synopsis;
        static constexpr std::size_t example_max_size = 4;
        MetadataMessage examples[example_max_size];
        LearnWebsiteLinkLiteral website_link;

        AutocompletePriority autocomplete_priority;

        size_t minimum_arity;
        size_t maximum_arity;

        CommandOptionsStructure options;

        std::vector<std::string> (*valid_arguments)(const VcpkgPaths& paths);

        LocalizedString get_example_text() const;
    };

    LocalizedString usage_for_command(const CommandMetadata& command_metadata);

    struct FeatureFlagSettings
    {
        bool registries;
        bool compiler_tracking;
        bool binary_caching;
        bool versions;
        bool dependency_graph;
    };

    struct PortApplicableSetting
    {
        std::string value;

        PortApplicableSetting(StringView setting);
        bool is_port_affected(StringView port_name) const noexcept;

    private:
        std::vector<std::string> affected_ports;
    };

    struct VcpkgCmdArguments
    {
        static VcpkgCmdArguments create_from_command_line(const ILineReader& fs,
                                                          const int argc,
                                                          const CommandLineCharType* const* const argv);
        static VcpkgCmdArguments create_from_arg_sequence(const std::string* arg_begin, const std::string* arg_end);

        Optional<std::string> vcpkg_root_dir_arg;
        Optional<std::string> vcpkg_root_dir_env;
        Optional<bool> force_classic_mode;
        Optional<std::string> manifest_root_dir;

        Optional<std::string> buildtrees_root_dir;
        Optional<std::string> downloads_root_dir;
        Optional<std::string> install_root_dir;
        Optional<std::string> packages_root_dir;
        Optional<std::string> scripts_root_dir;
        Optional<std::string> builtin_ports_root_dir;
        Optional<std::string> builtin_registry_versions_dir;
        Optional<std::string> registries_cache_dir;
        Optional<std::string> tools_data_file;

        Optional<std::string> default_visual_studio_path;

        Optional<std::string> triplet;
        Optional<std::string> host_triplet;
        std::vector<std::string> cli_overlay_ports;
        std::vector<std::string> env_overlay_ports;
        std::vector<std::string> cli_overlay_triplets;
        std::vector<std::string> env_overlay_triplets;

        std::vector<std::string> cli_binary_sources;
        Optional<std::string> env_binary_sources;
        Optional<std::string> nuget_id_prefix;
        Optional<bool> use_nuget_cache;
        Optional<std::string> vcpkg_nuget_repository;
        Optional<std::string> github_repository;
        Optional<std::string> github_server_url;
        Optional<std::string> github_ref;
        Optional<std::string> github_sha;
        Optional<std::string> ci_repository_id;
        Optional<std::string> ci_repository_owner_id;

        Optional<PortApplicableSetting> cmake_debug;
        Optional<PortApplicableSetting> cmake_configure_debug;

        std::vector<std::string> cmake_args;

        Optional<bool> exact_abi_tools_versions;

        Optional<bool> debug = nullopt;
        Optional<bool> debug_env = nullopt;
        Optional<bool> send_metrics = nullopt;
        // fully disable metrics -- both printing and sending
        Optional<bool> disable_metrics = nullopt;
        Optional<bool> print_metrics = nullopt;

        Optional<bool> wait_for_lock = nullopt;

        Optional<bool> ignore_lock_failures = nullopt;

        bool do_not_take_lock = false;

        Optional<std::string> github_run_id;
        Optional<std::string> github_token;
        Optional<std::string> github_job;
        Optional<std::string> github_workflow;

        // feature flags

        Optional<bool> dependency_graph_feature = nullopt;

        Optional<bool> feature_packages = nullopt;
        Optional<bool> binary_caching = nullopt;
        Optional<bool> compiler_tracking = nullopt;
        Optional<bool> registries_feature = nullopt;
        Optional<bool> versions_feature = nullopt;

        bool dependency_graph_enabled() const { return dependency_graph_feature.value_or(false); }
        bool binary_caching_enabled() const { return binary_caching.value_or(true); }
        bool compiler_tracking_enabled() const { return compiler_tracking.value_or(true); }
        bool registries_enabled() const { return registries_feature.value_or(true); }
        bool versions_enabled() const { return versions_feature.value_or(true); }
        FeatureFlagSettings feature_flag_settings() const
        {
            FeatureFlagSettings f;
            f.binary_caching = binary_caching_enabled();
            f.compiler_tracking = compiler_tracking_enabled();
            f.registries = registries_enabled();
            f.versions = versions_enabled();
            f.dependency_graph = dependency_graph_enabled();
            return f;
        }
        const Optional<StringLiteral>& detected_ci_environment_name() const { return m_detected_ci_environment_name; }
        CIKind detected_ci() const { return m_detected_ci_environment_type; }

        const std::string& get_command() const noexcept { return command; }

        std::vector<std::string> forwardable_arguments;

        ParsedArguments parse_arguments(const CommandMetadata& command_metadata) const;

        void imbue_from_environment();
        void imbue_from_fake_environment(const std::map<StringLiteral, std::string, std::less<>>& env);

        // Applies recursive settings from the environment or sets a global environment variable
        // to be consumed by subprocesses; may only be called once per process.
        static void imbue_or_apply_process_recursion(VcpkgCmdArguments& args);

        void check_feature_flag_consistency() const;

        void debug_print_feature_flags() const;
        void track_feature_flag_metrics() const;
        void track_environment_metrics() const;

        Optional<std::string> asset_sources_template() const;

        const std::vector<std::string>& get_forwardable_arguments() const noexcept;

    private:
        VcpkgCmdArguments(CmdParser&& parser);

        void imbue_from_environment_impl(std::function<Optional<std::string>(ZStringView)> get_env);

        Optional<std::string> asset_sources_template_env; // for EnvironmentVariableXVcpkgAssetSources
        Optional<std::string> asset_sources_template_arg; // for SwitchAssetSources

        std::string command;

        Optional<StringLiteral> m_detected_ci_environment_name;
        CIKind m_detected_ci_environment_type = CIKind::None;

        friend LocalizedString usage_for_command(const CommandMetadata& command_metadata);
        CmdParser parser;
    };
}
