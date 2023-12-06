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
        std::set<std::string, std::less<>> switches;
        std::map<std::string, std::string, std::less<>> settings;
        std::map<std::string, std::vector<std::string>, std::less<>> multisettings;

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

    void print_usage(const CommandMetadata& command_metadata);

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
        PortApplicableSetting(const PortApplicableSetting&);
        PortApplicableSetting(PortApplicableSetting&&);
        PortApplicableSetting& operator=(const PortApplicableSetting&);
        PortApplicableSetting& operator=(PortApplicableSetting&&);
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

        constexpr static StringLiteral VCPKG_ROOT_DIR_ENV = "VCPKG_ROOT";
        constexpr static StringLiteral VCPKG_ROOT_DIR_ARG = "vcpkg-root";

        constexpr static StringLiteral VCPKG_ROOT_ARG_NAME = "VCPKG_ROOT_ARG";
        Optional<std::string> vcpkg_root_dir_arg;
        constexpr static StringLiteral VCPKG_ROOT_ENV_NAME = "VCPKG_ROOT_ENV";
        Optional<std::string> vcpkg_root_dir_env;
        constexpr static StringLiteral MANIFEST_ROOT_DIR_ARG = "manifest-root";
        Optional<std::string> manifest_root_dir;

        constexpr static StringLiteral BUILDTREES_ROOT_DIR_ARG = "buildtrees-root";
        Optional<std::string> buildtrees_root_dir;
        constexpr static StringLiteral DOWNLOADS_ROOT_DIR_ENV = "VCPKG_DOWNLOADS";
        constexpr static StringLiteral DOWNLOADS_ROOT_DIR_ARG = "downloads-root";
        Optional<std::string> downloads_root_dir;
        constexpr static StringLiteral INSTALL_ROOT_DIR_ARG = "install-root";
        Optional<std::string> install_root_dir;
        constexpr static StringLiteral PACKAGES_ROOT_DIR_ARG = "packages-root";
        Optional<std::string> packages_root_dir;
        constexpr static StringLiteral SCRIPTS_ROOT_DIR_ARG = "scripts-root";
        Optional<std::string> scripts_root_dir;
        constexpr static StringLiteral BUILTIN_PORTS_ROOT_DIR_ARG = "builtin-ports-root";
        Optional<std::string> builtin_ports_root_dir;
        constexpr static StringLiteral BUILTIN_REGISTRY_VERSIONS_DIR_ARG = "builtin-registry-versions-dir";
        Optional<std::string> builtin_registry_versions_dir;
        constexpr static StringLiteral REGISTRIES_CACHE_DIR_ENV = "X_VCPKG_REGISTRIES_CACHE";
        constexpr static StringLiteral REGISTRIES_CACHE_DIR_ARG = "registries-cache";
        Optional<std::string> registries_cache_dir;

        constexpr static StringLiteral DEFAULT_VISUAL_STUDIO_PATH_ENV = "VCPKG_VISUAL_STUDIO_PATH";
        Optional<std::string> default_visual_studio_path;

        constexpr static StringLiteral TRIPLET_ENV = "VCPKG_DEFAULT_TRIPLET";
        constexpr static StringLiteral TRIPLET_ARG = "triplet";
        Optional<std::string> triplet;
        constexpr static StringLiteral HOST_TRIPLET_ENV = "VCPKG_DEFAULT_HOST_TRIPLET";
        constexpr static StringLiteral HOST_TRIPLET_ARG = "host-triplet";
        Optional<std::string> host_triplet;
        constexpr static StringLiteral OVERLAY_PORTS_ENV = "VCPKG_OVERLAY_PORTS";
        constexpr static StringLiteral OVERLAY_PORTS_ARG = "overlay-ports";
        std::vector<std::string> cli_overlay_ports;
        std::vector<std::string> env_overlay_ports;
        constexpr static StringLiteral OVERLAY_TRIPLETS_ENV = "VCPKG_OVERLAY_TRIPLETS";
        constexpr static StringLiteral OVERLAY_TRIPLETS_ARG = "overlay-triplets";
        std::vector<std::string> cli_overlay_triplets;
        std::vector<std::string> env_overlay_triplets;

        constexpr static StringLiteral BINARY_SOURCES_ENV = "VCPKG_BINARY_SOURCES";
        constexpr static StringLiteral BINARY_SOURCES_ARG = "binarysource";
        std::vector<std::string> cli_binary_sources;
        Optional<std::string> env_binary_sources;
        constexpr static StringLiteral ACTIONS_CACHE_URL_ENV = "ACTIONS_CACHE_URL";
        Optional<std::string> actions_cache_url;
        constexpr static StringLiteral ACTIONS_RUNTIME_TOKEN_ENV = "ACTIONS_RUNTIME_TOKEN";
        Optional<std::string> actions_runtime_token;
        constexpr static StringLiteral NUGET_ID_PREFIX_ENV = "X_VCPKG_NUGET_ID_PREFIX";
        Optional<std::string> nuget_id_prefix;
        constexpr static StringLiteral VCPKG_USE_NUGET_CACHE_ENV = "VCPKG_USE_NUGET_CACHE";
        Optional<bool> use_nuget_cache;
        constexpr static StringLiteral VCPKG_NUGET_REPOSITORY_ENV = "VCPKG_NUGET_REPOSITORY";
        Optional<std::string> vcpkg_nuget_repository;
        constexpr static StringLiteral GITHUB_REPOSITORY_ENV = "GITHUB_REPOSITORY";
        Optional<std::string> github_repository;
        constexpr static StringLiteral GITHUB_SERVER_URL_ENV = "GITHUB_SERVER_URL";
        Optional<std::string> github_server_url;
        constexpr static StringLiteral GITHUB_REF_ENV = "GITHUB_REF";
        Optional<std::string> github_ref;
        constexpr static StringLiteral GITHUB_SHA_ENV = "GITHUB_SHA";
        Optional<std::string> github_sha;
        constexpr static StringLiteral GITHUB_REPOSITORY_OWNER_ID = "GITHUB_REPOSITORY_OWNER_ID";
        Optional<std::string> ci_repository_id;
        Optional<std::string> ci_repository_owner_id;

        constexpr static StringLiteral CMAKE_DEBUGGING_ARG = "cmake-debug";
        Optional<PortApplicableSetting> cmake_debug;
        constexpr static StringLiteral CMAKE_CONFIGURE_DEBUGGING_ARG = "cmake-configure-debug";
        Optional<PortApplicableSetting> cmake_configure_debug;

        constexpr static StringLiteral CMAKE_SCRIPT_ARG = "cmake-args";
        std::vector<std::string> cmake_args;

        constexpr static StringLiteral EXACT_ABI_TOOLS_VERSIONS_SWITCH = "abi-tools-use-exact-versions";
        Optional<bool> exact_abi_tools_versions;

        constexpr static StringLiteral DEBUG_SWITCH = "debug";
        Optional<bool> debug = nullopt;
        constexpr static StringLiteral DEBUG_ENV_SWITCH = "debug-env";
        Optional<bool> debug_env = nullopt;
        constexpr static StringLiteral SEND_METRICS_SWITCH = "sendmetrics";
        Optional<bool> send_metrics = nullopt;
        // fully disable metrics -- both printing and sending
        constexpr static StringLiteral DISABLE_METRICS_ENV = "VCPKG_DISABLE_METRICS";
        constexpr static StringLiteral DISABLE_METRICS_SWITCH = "disable-metrics";
        Optional<bool> disable_metrics = nullopt;
        constexpr static StringLiteral PRINT_METRICS_SWITCH = "printmetrics";
        Optional<bool> print_metrics = nullopt;

        constexpr static StringLiteral WAIT_FOR_LOCK_SWITCH = "wait-for-lock";
        Optional<bool> wait_for_lock = nullopt;

        constexpr static StringLiteral IGNORE_LOCK_FAILURES_SWITCH = "ignore-lock-failures";
        constexpr static StringLiteral IGNORE_LOCK_FAILURES_ENV = "X_VCPKG_IGNORE_LOCK_FAILURES";
        Optional<bool> ignore_lock_failures = nullopt;

        bool do_not_take_lock = false;

        constexpr static StringLiteral ASSET_SOURCES_ENV = "X_VCPKG_ASSET_SOURCES";
        constexpr static StringLiteral ASSET_SOURCES_ARG = "asset-sources";

        constexpr static StringLiteral GITHUB_RUN_ID_ENV = "GITHUB_RUN_ID";
        Optional<std::string> github_run_id;
        constexpr static StringLiteral GITHUB_TOKEN_ENV = "GITHUB_TOKEN";
        Optional<std::string> github_token;
        constexpr static StringLiteral GITHUB_JOB_ENV = "GITHUB_JOB";
        Optional<std::string> github_job;
        constexpr static StringLiteral GITHUB_WORKFLOW_ENV = "GITHUB_WORKFLOW";
        Optional<std::string> github_workflow;

        // feature flags
        constexpr static StringLiteral FEATURE_FLAGS_ENV = "VCPKG_FEATURE_FLAGS";
        constexpr static StringLiteral FEATURE_FLAGS_ARG = "feature-flags";

        constexpr static StringLiteral DEPENDENCY_GRAPH_FEATURE = "dependencygraph";
        Optional<bool> dependency_graph_feature = nullopt;

        constexpr static StringLiteral FEATURE_PACKAGES_SWITCH = "featurepackages";
        Optional<bool> feature_packages = nullopt;
        constexpr static StringLiteral BINARY_CACHING_FEATURE = "binarycaching";
        constexpr static StringLiteral BINARY_CACHING_SWITCH = "binarycaching";
        Optional<bool> binary_caching = nullopt;
        constexpr static StringLiteral COMPILER_TRACKING_FEATURE = "compilertracking";
        Optional<bool> compiler_tracking = nullopt;
        constexpr static StringLiteral MANIFEST_MODE_FEATURE = "manifests";
        constexpr static StringLiteral REGISTRIES_FEATURE = "registries";
        Optional<bool> registries_feature = nullopt;
        constexpr static StringLiteral VERSIONS_FEATURE = "versions";
        Optional<bool> versions_feature = nullopt;

        constexpr static StringLiteral RECURSIVE_DATA_ENV = "X_VCPKG_RECURSIVE_DATA";

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
        const Optional<StringLiteral>& detected_ci_environment() const { return m_detected_ci_environment; }

        const std::string& get_command() const noexcept { return command; }

        std::vector<std::string> forwardable_arguments;

        ParsedArguments parse_arguments(const CommandMetadata& command_metadata) const;

        void imbue_from_environment();
        void imbue_from_fake_environment(const std::map<std::string, std::string, std::less<>>& env);

        // Applies recursive settings from the environment or sets a global environment variable
        // to be consumed by subprocesses; may only be called once per process.
        static void imbue_or_apply_process_recursion(VcpkgCmdArguments& args);

        void check_feature_flag_consistency() const;

        void debug_print_feature_flags() const;
        void track_feature_flag_metrics() const;
        void track_environment_metrics() const;

        Optional<std::string> asset_sources_template() const;

        const std::vector<std::string>& get_forwardable_arguments() const noexcept;

        VcpkgCmdArguments(const VcpkgCmdArguments&);
        VcpkgCmdArguments(VcpkgCmdArguments&&);
        VcpkgCmdArguments& operator=(const VcpkgCmdArguments&);
        VcpkgCmdArguments& operator=(VcpkgCmdArguments&&);
        ~VcpkgCmdArguments();

    private:
        VcpkgCmdArguments(CmdParser&& parser);

        void imbue_from_environment_impl(std::function<Optional<std::string>(ZStringView)> get_env);

        Optional<std::string> asset_sources_template_env; // for ASSET_SOURCES_ENV
        Optional<std::string> asset_sources_template_arg; // for ASSET_SOURCES_ARG

        std::string command;

        Optional<StringLiteral> m_detected_ci_environment;

        friend void print_usage(const CommandMetadata& command_metadata);
        CmdParser parser;
    };
}
