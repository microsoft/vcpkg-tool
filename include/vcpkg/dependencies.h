#pragma once

#include <vcpkg/base/fwd/graphs.h>

#include <vcpkg/fwd/cmakevars.h>
#include <vcpkg/fwd/portfileprovider.h>

#include <vcpkg/base/optional.h>

#include <vcpkg/commands.build.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/statusparagraph.h>

#include <map>
#include <string>
#include <vector>

namespace vcpkg
{
    [[nodiscard]] StringLiteral request_type_indent(RequestType request_type);

    struct BasicAction
    {
        static bool compare_by_name(const BasicAction* left, const BasicAction* right);

        PackageSpec spec;
    };

    struct PackageAction : BasicAction
    {
        std::vector<PackageSpec> package_dependencies;
        InternalFeatureSet feature_list;
    };

    struct InstallPlanAction : PackageAction
    {
        InstallPlanAction(const InstallPlanAction&) = delete;
        InstallPlanAction(InstallPlanAction&&) = default;
        InstallPlanAction& operator=(const InstallPlanAction&) = delete;
        InstallPlanAction& operator=(InstallPlanAction&&) = default;

        InstallPlanAction(InstalledPackageView&& spghs,
                          RequestType request_type,
                          UseHeadVersion use_head_version,
                          Editable editable);

        InstallPlanAction(const PackageSpec& spec,
                          const SourceControlFileAndLocation& scfl,
                          PackagesDirAssigner& packages_dir_assigner,
                          RequestType request_type,
                          UseHeadVersion use_head_version,
                          Editable editable,
                          std::map<std::string, std::vector<FeatureSpec>>&& dependencies,
                          std::vector<LocalizedString>&& build_failure_messages,
                          std::vector<std::string> default_features);

        const std::string& public_abi() const;
        bool has_package_abi() const;
        Optional<const std::string&> package_abi() const;
        const PreBuildInfo& pre_build_info(LineInfo li) const;
        Version version() const;
        std::string display_name() const;

        Optional<const SourceControlFileAndLocation&> source_control_file_and_location;
        Optional<InstalledPackageView> installed_package;
        Optional<std::vector<std::string>> default_features;

        InstallPlanType plan_type;
        RequestType request_type;
        UseHeadVersion use_head_version;
        Editable editable;

        std::map<std::string, std::vector<FeatureSpec>> feature_dependencies;
        std::vector<LocalizedString> build_failure_messages;

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
        ActionPlan() = default;
        ActionPlan& operator=(const ActionPlan&) = default;
        ActionPlan(const ActionPlan&) = default;
        ActionPlan& operator=(ActionPlan&&) = default;
        ActionPlan(ActionPlan&&) noexcept = default;

        bool empty() const { return remove_actions.empty() && already_installed.empty() && install_actions.empty(); }
        size_t size() const { return remove_actions.size() + already_installed.size() + install_actions.size(); }
        void print_unsupported_warnings();

        std::vector<RemovePlanAction> remove_actions;
        std::vector<InstallPlanAction> already_installed;
        std::vector<InstallPlanAction> install_actions;
        std::map<FeatureSpec, PlatformExpression::Expr> unsupported_features;
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
        CreateInstallPlanOptions(GraphRandomizer* randomizer,
                                 Triplet host_triplet,
                                 UnsupportedPortAction action,
                                 UseHeadVersion use_head_version_if_user_requested,
                                 Editable editable_if_user_requested)
            : randomizer(randomizer)
            , host_triplet(host_triplet)
            , unsupported_port_action(action)
            , use_head_version_if_user_requested(use_head_version_if_user_requested)
            , editable_if_user_requested(editable_if_user_requested)
        {
        }

        GraphRandomizer* randomizer;
        Triplet host_triplet;
        UnsupportedPortAction unsupported_port_action;
        UseHeadVersion use_head_version_if_user_requested;
        Editable editable_if_user_requested;
    };

    struct CreateUpgradePlanOptions
    {
        CreateUpgradePlanOptions(GraphRandomizer* randomizer, Triplet host_triplet, UnsupportedPortAction action)
            : randomizer(randomizer), host_triplet(host_triplet), unsupported_port_action(action)
        {
        }

        GraphRandomizer* randomizer;
        Triplet host_triplet;
        UnsupportedPortAction unsupported_port_action;
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
                                           PackagesDirAssigner& packages_dir_assigner,
                                           const CreateInstallPlanOptions& options);

    ActionPlan create_upgrade_plan(const PortFileProvider& provider,
                                   const CMakeVars::CMakeVarProvider& var_provider,
                                   const std::vector<PackageSpec>& specs,
                                   const StatusParagraphs& status_db,
                                   PackagesDirAssigner& packages_dir_assigner,
                                   const CreateUpgradePlanOptions& options);

    ExpectedL<ActionPlan> create_versioned_install_plan(const IVersionedPortfileProvider& vprovider,
                                                        const IBaselineProvider& bprovider,
                                                        const IOverlayProvider& oprovider,
                                                        const CMakeVars::CMakeVarProvider& var_provider,
                                                        const std::vector<Dependency>& deps,
                                                        const std::vector<DependencyOverride>& overrides,
                                                        const PackageSpec& toplevel,
                                                        PackagesDirAssigner& packages_dir_assigner,
                                                        const CreateInstallPlanOptions& options);

    struct FormattedPlan
    {
        bool has_removals = false;
        LocalizedString warning_text;
        LocalizedString normal_text;

        LocalizedString all_text() const;
    };

    FormattedPlan format_plan(const ActionPlan& action_plan);

    FormattedPlan print_plan(const ActionPlan& action_plan);
}
