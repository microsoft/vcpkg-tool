#pragma once

#include <vcpkg/fwd/cmakevars.h>
#include <vcpkg/fwd/portfileprovider.h>

#include <vcpkg/base/optional.h>

#include <vcpkg/commands.build.h>
#include <vcpkg/packagespec.h>

#include <functional>
#include <map>
#include <vector>

namespace vcpkg
{
    struct GraphRandomizer;

    enum class UnsupportedPortAction : bool
    {
        Warn,
        Error,
    };

    enum class RequestType
    {
        UNKNOWN,
        USER_REQUESTED,
        AUTO_SELECTED
    };

    [[nodiscard]] StringLiteral request_type_indent(RequestType request_type);

    enum class InstallPlanType
    {
        UNKNOWN,
        BUILD_AND_INSTALL,
        ALREADY_INSTALLED,
        EXCLUDED
    };

    struct BasicAction
    {
        static bool compare_by_name(const BasicAction* left, const BasicAction* right);

        PackageSpec spec;
    };

    struct PackageAction : BasicAction
    {
        std::string displayname() const;

        std::vector<PackageSpec> package_dependencies;
        InternalFeatureSet feature_list;
    };

    struct InstallPlanAction : PackageAction
    {
        InstallPlanAction(const InstallPlanAction&) = delete;
        InstallPlanAction(InstallPlanAction&&) = default;
        InstallPlanAction& operator=(const InstallPlanAction&) = delete;
        InstallPlanAction& operator=(InstallPlanAction&&) = default;

        InstallPlanAction(InstalledPackageView&& spghs, const RequestType& request_type);

        InstallPlanAction(const PackageSpec& spec,
                          const SourceControlFileAndLocation& scfl,
                          const RequestType& request_type,
                          Triplet host_triplet,
                          std::map<std::string, std::vector<FeatureSpec>>&& dependencies,
                          std::vector<LocalizedString>&& build_failure_messages);

        const std::string& public_abi() const;
        bool has_package_abi() const;
        Optional<const std::string&> package_abi() const;
        const PreBuildInfo& pre_build_info(LineInfo li) const;
        Version version() const;

        Optional<const SourceControlFileAndLocation&> source_control_file_and_location;
        Optional<InstalledPackageView> installed_package;

        InstallPlanType plan_type;
        RequestType request_type;
        BuildPackageOptions build_options;

        std::map<std::string, std::vector<FeatureSpec>> feature_dependencies;
        std::vector<LocalizedString> build_failure_messages;
        Triplet host_triplet;

        // only valid with source_control_file_and_location
        Optional<AbiInfo> abi_info;
        Optional<Path> package_dir;
    };

    struct NotInstalledAction : BasicAction
    {
        NotInstalledAction(const PackageSpec& spec);
    };

    struct RemovePlanAction : BasicAction
    {
        RemovePlanAction(const PackageSpec& spec, RequestType rt);

        RequestType request_type;
    };

    struct ActionPlan
    {
        bool empty() const { return remove_actions.empty() && already_installed.empty() && install_actions.empty(); }
        size_t size() const { return remove_actions.size() + already_installed.size() + install_actions.size(); }
        void print_unsupported_warnings();

        std::vector<RemovePlanAction> remove_actions;
        std::vector<InstallPlanAction> already_installed;
        std::vector<InstallPlanAction> install_actions;
        std::map<FeatureSpec, PlatformExpression::Expr> unsupported_features;
    };

    enum class ExportPlanType
    {
        UNKNOWN,
        NOT_BUILT,
        ALREADY_BUILT
    };

    struct ExportPlanAction : BasicAction
    {
        ExportPlanAction(const ExportPlanAction&) = delete;
        ExportPlanAction(ExportPlanAction&&) = default;
        ExportPlanAction& operator=(const ExportPlanAction&) = delete;
        ExportPlanAction& operator=(ExportPlanAction&&) = default;

        ExportPlanAction(const PackageSpec& spec, InstalledPackageView&& installed_package, RequestType request_type);

        ExportPlanAction(const PackageSpec& spec, RequestType request_type);

        ExportPlanType plan_type;
        RequestType request_type;

        Optional<const BinaryParagraph&> core_paragraph() const;
        std::vector<PackageSpec> dependencies() const;

    private:
        Optional<InstalledPackageView> m_installed_package;
    };

    struct CreateInstallPlanOptions
    {
        CreateInstallPlanOptions(GraphRandomizer* r, Triplet t, const Path& p)
            : randomizer(r), host_triplet(t), packages_dir(p)
        {
        }
        CreateInstallPlanOptions(Triplet t, const Path& p, UnsupportedPortAction action = UnsupportedPortAction::Error)
            : host_triplet(t), packages_dir(p), unsupported_port_action(action)
        {
        }

        GraphRandomizer* randomizer = nullptr;
        Triplet host_triplet;
        Path packages_dir;
        UnsupportedPortAction unsupported_port_action = UnsupportedPortAction::Warn;
    };

    struct RemovePlan
    {
        bool empty() const;
        bool has_non_user_requested() const;

        std::vector<NotInstalledAction> not_installed;
        std::vector<RemovePlanAction> remove;
    };

    RemovePlan create_remove_plan(const std::vector<PackageSpec>& specs, const StatusParagraphs& status_db);

    std::vector<ExportPlanAction> create_export_plan(const std::vector<PackageSpec>& specs,
                                                     const StatusParagraphs& status_db);

    /// <summary>Figure out which actions are required to install features specifications in `specs`.</summary>
    /// <param name="provider">Contains the ports of the current environment.</param>
    /// <param name="specs">Feature specifications to resolve dependencies for.</param>
    /// <param name="status_db">Status of installed packages in the current environment.</param>
    ActionPlan create_feature_install_plan(const PortFileProvider& provider,
                                           const CMakeVars::CMakeVarProvider& var_provider,
                                           View<FullPackageSpec> specs,
                                           const StatusParagraphs& status_db,
                                           const CreateInstallPlanOptions& options);

    ActionPlan create_upgrade_plan(const PortFileProvider& provider,
                                   const CMakeVars::CMakeVarProvider& var_provider,
                                   const std::vector<PackageSpec>& specs,
                                   const StatusParagraphs& status_db,
                                   const CreateInstallPlanOptions& options);

    ExpectedL<ActionPlan> create_versioned_install_plan(const IVersionedPortfileProvider& vprovider,
                                                        const IBaselineProvider& bprovider,
                                                        const IOverlayProvider& oprovider,
                                                        const CMakeVars::CMakeVarProvider& var_provider,
                                                        const std::vector<Dependency>& deps,
                                                        const std::vector<DependencyOverride>& overrides,
                                                        const PackageSpec& toplevel,
                                                        const CreateInstallPlanOptions& options);

    struct FormattedPlan
    {
        bool has_removals = false;
        LocalizedString text;
    };

    FormattedPlan format_plan(const ActionPlan& action_plan, const Path& builtin_ports_dir);

    void print_plan(const ActionPlan& action_plan, const bool is_recursive, const Path& builtin_ports_dir);
}
