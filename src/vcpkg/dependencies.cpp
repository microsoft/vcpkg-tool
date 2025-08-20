#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/graphs.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/cmakevars.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/documentation.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkglib.h>

#include <unordered_map>
#include <unordered_set>

namespace vcpkg
{
    namespace
    {
        struct ClusterGraph;

        struct ClusterInstalled
        {
            ClusterInstalled(const InstalledPackageView& ipv) : ipv(ipv)
            {
                original_features.emplace(FeatureNameCore);
                for (auto&& feature : ipv.features)
                {
                    original_features.emplace(feature->package.feature);
                }
            }

            InstalledPackageView ipv;
            std::unordered_set<PackageSpec> remove_edges;
            std::unordered_set<std::string> original_features;

            // Tracks whether an incoming request has asked for the default features -- on reinstall, add them
            bool defaults_requested = false;
        };

        struct ClusterInstallInfo
        {
            std::map<std::string, std::vector<FeatureSpec>> build_edges;
            std::map<PackageSpec, std::set<Version, VersionMapLess>> version_constraints;
            bool defaults_requested = false;
            std::vector<std::string> default_features;
            bool reduced_defaults = false;
        };

        /// <summary>
        /// Representation of a package and its features in a ClusterGraph.
        /// </summary>
        struct Cluster
        {
            Cluster(const InstalledPackageView& ipv, ExpectedL<const SourceControlFileAndLocation&>&& scfl)
                : m_spec(ipv.spec()), m_scfl(std::move(scfl)), m_installed(ipv)
            {
            }

            Cluster(const PackageSpec& spec, const SourceControlFileAndLocation& scfl) : m_spec(spec), m_scfl(scfl) { }

            Cluster(const Cluster&) = delete;
            Cluster(Cluster&&) = default;
            Cluster& operator=(const Cluster&) = delete;
            Cluster& operator=(Cluster&&) = default;

            bool has_feature_installed(const std::string& feature) const
            {
                if (const ClusterInstalled* inst = m_installed.get())
                {
                    return Util::Sets::contains(inst->original_features, feature);
                }
                return false;
            }

            bool has_defaults_installed() const
            {
                if (const ClusterInstalled* inst = m_installed.get())
                {
                    return std::all_of(inst->ipv.core->package.default_features.begin(),
                                       inst->ipv.core->package.default_features.end(),
                                       [&](const std::string& feature) {
                                           return Util::Sets::contains(inst->original_features, feature);
                                       });
                }
                return false;
            }

            // Returns dependencies which were added as a result of this call.
            // Precondition: must have called "mark_for_reinstall()" or "create_install_info()" on this cluster
            void add_feature(const std::string& feature,
                             const CMakeVars::CMakeVarProvider& var_provider,
                             std::vector<FeatureSpec>& out_new_dependencies,
                             Triplet host_triplet)
            {
                const auto& scfl = get_scfl_or_exit();

                ClusterInstallInfo& info = m_install_info.value_or_exit(VCPKG_LINE_INFO);
                if (feature == FeatureNameDefault)
                {
                    if (!info.defaults_requested)
                    {
                        if (Util::any_of(scfl.source_control_file->core_paragraph->default_features,
                                         [](const auto& feature) { return !feature.platform.is_empty(); }))
                        {
                            if (auto maybe_vars = var_provider.get_dep_info_vars(m_spec))
                            {
                                info.defaults_requested = true;
                                for (auto&& f : scfl.source_control_file->core_paragraph->default_features)
                                {
                                    if (f.platform.evaluate(maybe_vars.value_or_exit(VCPKG_LINE_INFO)))
                                    {
                                        info.default_features.push_back(f.name);
                                    }
                                }
                            }
                        }
                        else
                        {
                            info.defaults_requested = true;
                            for (auto&& f : scfl.source_control_file->core_paragraph->default_features)
                                info.default_features.push_back(f.name);
                        }

                        if (info.reduced_defaults)
                        {
                            info.reduced_defaults = false;
                            // If the user did not explicitly request this installation, we need to add all new default
                            // features
                            std::set<std::string> defaults_set{info.default_features.begin(),
                                                               info.default_features.end()};

                            // Install only features that were not previously available
                            if (auto p_inst = m_installed.get())
                            {
                                for (auto&& prev_default : p_inst->ipv.core->package.default_features)
                                {
                                    defaults_set.erase(prev_default);
                                }
                            }

                            for (const std::string& default_feature : defaults_set)
                            {
                                // Instead of dealing with adding default features to each of our dependencies right
                                // away we just defer to the next pass of the loop.
                                out_new_dependencies.emplace_back(m_spec, default_feature);
                            }
                        }
                        else
                        {
                            for (auto&& default_feature : std::move(info.default_features))
                            {
                                out_new_dependencies.emplace_back(m_spec, std::move(default_feature));
                            }
                        }
                    }
                    return;
                }

                if (Util::Sets::contains(info.build_edges, feature))
                {
                    // This feature has already been completely handled
                    return;
                }
                auto maybe_vars = var_provider.get_dep_info_vars(m_spec);
                Optional<const std::vector<Dependency>&> maybe_qualified_deps =
                    scfl.source_control_file->find_dependencies_for_feature(feature);
                if (!maybe_qualified_deps.has_value())
                {
                    Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                                  msgFailedToFindPortFeature,
                                                  msg::feature = feature,
                                                  msg::package_name = m_spec.name());
                }
                const std::vector<Dependency>* qualified_deps = &maybe_qualified_deps.value_or_exit(VCPKG_LINE_INFO);

                std::vector<FeatureSpec> dep_list;
                if (auto vars = maybe_vars.get())
                {
                    // Qualified dependency resolution is available
                    for (auto&& dep : *qualified_deps)
                    {
                        if (dep.platform.evaluate(*vars))
                        {
                            std::vector<std::string> features;
                            features.reserve(dep.features.size());
                            for (const auto& f : dep.features)
                            {
                                if (f.platform.evaluate(*vars))
                                {
                                    features.push_back(f.name);
                                }
                            }
                            auto fullspec = dep.to_full_spec(features, m_spec.triplet(), host_triplet);
                            fullspec.expand_fspecs_to(dep_list);
                            if (auto opt = dep.constraint.try_get_minimum_version())
                            {
                                info.version_constraints[fullspec.package_spec].insert(
                                    std::move(opt).value_or_exit(VCPKG_LINE_INFO));
                            }
                        }
                    }

                    Util::sort_unique_erase(dep_list);
                    info.build_edges.emplace(feature, dep_list);
                }
                else
                {
                    bool requires_qualified_resolution = false;
                    for (const Dependency& dep : *qualified_deps)
                    {
                        if (!dep.has_platform_expressions())
                        {
                            auto fullspec =
                                dep.to_full_spec(Util::fmap(dep.features, [](const auto& f) { return f.name; }),
                                                 m_spec.triplet(),
                                                 host_triplet);
                            fullspec.expand_fspecs_to(dep_list);
                            if (auto opt = dep.constraint.try_get_minimum_version())
                            {
                                info.version_constraints[fullspec.package_spec].insert(
                                    std::move(opt).value_or_exit(VCPKG_LINE_INFO));
                            }
                        }
                        else
                        {
                            requires_qualified_resolution = true;
                        }
                    }
                    Util::sort_unique_erase(dep_list);
                    if (requires_qualified_resolution)
                    {
                        auto my_spec = this->m_spec;
                        Util::erase_remove_if(dep_list, [my_spec](FeatureSpec& f) { return f.spec() == my_spec; });
                    }
                    else
                    {
                        info.build_edges.emplace(feature, dep_list);
                    }
                }

                Util::Vectors::append(out_new_dependencies, std::move(dep_list));
            }

            void create_install_info(std::vector<FeatureSpec>& out_reinstall_requirements)
            {
                bool defaults_requested = false;
                if (const ClusterInstalled* inst = m_installed.get())
                {
                    out_reinstall_requirements.emplace_back(m_spec, FeatureNameCore);
                    auto& scfl = get_scfl_or_exit();
                    for (const std::string& installed_feature : inst->original_features)
                    {
                        if (scfl.source_control_file->find_feature(installed_feature).has_value())
                            out_reinstall_requirements.emplace_back(m_spec, installed_feature);
                    }
                    defaults_requested = inst->defaults_requested;
                }

                Checks::check_exit(VCPKG_LINE_INFO, !m_install_info.has_value());
                m_install_info.emplace();

                if (defaults_requested)
                {
                    out_reinstall_requirements.emplace_back(m_spec, FeatureNameDefault);
                }
                else if (request_type != RequestType::USER_REQUESTED)
                {
                    out_reinstall_requirements.emplace_back(m_spec, FeatureNameDefault);
                    m_install_info.get()->reduced_defaults = true;
                }
            }

            const SourceControlFileAndLocation& get_scfl_or_exit() const
            {
                if (auto scfl = m_scfl.get())
                {
                    return *scfl;
                }

                Checks::msg_exit_with_error(
                    VCPKG_LINE_INFO,
                    msg::format(msgFailedToLoadInstalledManifest, msg::package_name = m_spec.name())
                        .append_raw('\n')
                        .append_raw(m_scfl.error()));
            }

            Optional<const PlatformExpression::Expr&> get_applicable_supports_expression(const FeatureSpec& spec) const
            {
                if (spec.feature() == FeatureNameCore)
                {
                    return get_scfl_or_exit().source_control_file->core_paragraph->supports_expression;
                }
                else if (spec.feature() != FeatureNameDefault)
                {
                    auto maybe_paragraph = get_scfl_or_exit().source_control_file->find_feature(spec.feature());
                    Checks::msg_check_maybe_upgrade(VCPKG_LINE_INFO,
                                                    maybe_paragraph.has_value(),
                                                    msgFailedToFindPortFeature,
                                                    msg::feature = spec.feature(),
                                                    msg::package_name = spec.port());

                    return maybe_paragraph.get()->supports_expression;
                }
                return nullopt;
            }

            Optional<Version> get_version() const
            {
                if (auto p_installed = m_installed.get())
                {
                    return p_installed->ipv.core->package.version;
                }
                else if (auto p_scfl = m_scfl.get())
                {
                    return p_scfl->to_version();
                }
                else
                    return nullopt;
            }

            PackageSpec m_spec;
            ExpectedL<const SourceControlFileAndLocation&> m_scfl;

            Optional<ClusterInstalled> m_installed;
            Optional<ClusterInstallInfo> m_install_info;

            RequestType request_type = RequestType::AUTO_SELECTED;
        };

        struct PackageGraph
        {
            PackageGraph(const PortFileProvider& provider,
                         const CMakeVars::CMakeVarProvider& var_provider,
                         const StatusParagraphs& status_db,
                         Triplet host_triplet,
                         PackagesDirAssigner& packages_dir_assigner);
            ~PackageGraph() = default;

            void install(Span<const FeatureSpec> specs, UnsupportedPortAction unsupported_port_action);
            void upgrade(Span<const PackageSpec> specs, UnsupportedPortAction unsupported_port_action);
            void mark_user_requested(const PackageSpec& spec);

            ActionPlan serialize(GraphRandomizer* randomizer,
                                 UseHeadVersion use_head_version_if_user_requested,
                                 Editable editable_if_user_requested) const;

            void mark_for_reinstall(const PackageSpec& spec,
                                    std::vector<FeatureSpec>& out_reinstall_requirements) const;
            const CMakeVars::CMakeVarProvider& m_var_provider;

            std::unique_ptr<ClusterGraph> m_graph;
            PackagesDirAssigner& m_packages_dir_assigner;
            std::map<FeatureSpec, PlatformExpression::Expr> m_unsupported_features;
        };

        /// <summary>
        /// Directional graph representing a collection of packages with their features connected by their dependencies.
        /// </summary>
        struct ClusterGraph
        {
            explicit ClusterGraph(const PortFileProvider& port_provider, Triplet host_triplet)
                : m_port_provider(port_provider), m_host_triplet(host_triplet)
            {
            }

            ClusterGraph(const ClusterGraph&) = delete;
            ClusterGraph& operator=(const ClusterGraph&) = delete;

            /// <summary>
            ///     Find the cluster associated with spec or if not found, create it from the PortFileProvider.
            /// </summary>
            /// <param name="spec">Package spec to get the cluster for.</param>
            /// <returns>The cluster found or created for spec.</returns>
            Cluster& get(const PackageSpec& spec)
            {
                auto it = m_graph.find(spec);
                if (it == m_graph.end())
                {
                    auto maybe_scfl = m_port_provider.get_control_file(spec.name());
                    if (auto scfl = maybe_scfl.get())
                    {
                        it = m_graph
                                 .emplace(std::piecewise_construct,
                                          std::forward_as_tuple(spec),
                                          std::forward_as_tuple(spec, *scfl))
                                 .first;
                    }
                    else
                    {
                        Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                                    msg::format(msgWhileLookingForSpec, msg::spec = spec)
                                                        .append_raw('\n')
                                                        .append_raw(maybe_scfl.error()));
                    }
                }

                return it->second;
            }

            Cluster& insert(const InstalledPackageView& ipv)
            {
                ExpectedL<const SourceControlFileAndLocation&> maybe_scfl =
                    m_port_provider.get_control_file(ipv.spec().name());

                return m_graph
                    .emplace(std::piecewise_construct,
                             std::forward_as_tuple(ipv.spec()),
                             std::forward_as_tuple(ipv, std::move(maybe_scfl)))
                    .first->second;
            }

            const Cluster& find_or_exit(const PackageSpec& spec, LineInfo li) const
            {
                auto it = m_graph.find(spec);
                Checks::msg_check_exit(li, it != m_graph.end(), msgFailedToLocateSpec, msg::spec = spec);
                return it->second;
            }

            auto begin() const { return m_graph.begin(); }
            auto end() const { return m_graph.end(); }

        private:
            std::map<PackageSpec, Cluster> m_graph;
            const PortFileProvider& m_port_provider;

        public:
            const Triplet m_host_triplet;
        };
    }

    static void format_plan_ipa_row(LocalizedString& out, bool add_head_tag, const InstallPlanAction& action)
    {
        out.append_raw(request_type_indent(action.request_type)).append_raw(action.display_name());
        if (add_head_tag && action.use_head_version == UseHeadVersion::Yes)
        {
            out.append_raw(" (+HEAD)");
        }
        if (auto scfl = action.source_control_file_and_location.get())
        {
            switch (scfl->kind)
            {
                case PortSourceKind::Unknown:
                case PortSourceKind::Builtin:
                    // intentionally empty
                    break;
                case PortSourceKind::Overlay:
                case PortSourceKind::Filesystem: out.append_raw(" -- ").append_raw(scfl->port_directory()); break;
                case PortSourceKind::Git: out.append_raw(" -- ").append_raw(scfl->spdx_location); break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }
    }

    StringLiteral request_type_indent(RequestType request_type)
    {
        switch (request_type)
        {
            case RequestType::AUTO_SELECTED: return "  * ";
            case RequestType::USER_REQUESTED: return "    ";
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    bool BasicAction::compare_by_name(const BasicAction* left, const BasicAction* right)
    {
        return left->spec.name() < right->spec.name();
    }

    static std::vector<PackageSpec> fdeps_to_pdeps(const PackageSpec& self,
                                                   const std::map<std::string, std::vector<FeatureSpec>>& dependencies)
    {
        std::set<PackageSpec> specs;
        for (auto&& p : dependencies)
        {
            for (auto&& q : p.second)
            {
                specs.insert(q.spec());
            }
        }
        specs.erase(self);
        return {specs.begin(), specs.end()};
    }

    static InternalFeatureSet fdeps_to_feature_list(const std::map<std::string, std::vector<FeatureSpec>>& fdeps)
    {
        InternalFeatureSet ret;
        for (auto&& d : fdeps)
        {
            ret.push_back(d.first);
        }
        return ret;
    }

    InstallPlanAction::InstallPlanAction(InstalledPackageView&& ipv,
                                         RequestType request_type,
                                         UseHeadVersion use_head_version,
                                         Editable editable)
        : PackageAction{{ipv.spec()}, ipv.dependencies(), ipv.feature_list()}
        , installed_package(std::move(ipv))
        , plan_type(InstallPlanType::ALREADY_INSTALLED)
        , request_type(request_type)
        , use_head_version(use_head_version)
        , editable(editable)
        , feature_dependencies(installed_package.get()->feature_dependencies())
    {
    }

    InstallPlanAction::InstallPlanAction(const PackageSpec& spec,
                                         const SourceControlFileAndLocation& scfl,
                                         PackagesDirAssigner& packages_dir_assigner,
                                         RequestType request_type,
                                         UseHeadVersion use_head_version,
                                         Editable editable,
                                         std::map<std::string, std::vector<FeatureSpec>>&& dependencies,
                                         std::vector<LocalizedString>&& build_failure_messages,
                                         std::vector<std::string> default_features)
        : PackageAction{{spec}, fdeps_to_pdeps(spec, dependencies), fdeps_to_feature_list(dependencies)}
        , source_control_file_and_location(scfl)
        , default_features(std::move(default_features))
        , plan_type(InstallPlanType::BUILD_AND_INSTALL)
        , request_type(request_type)
        , use_head_version(use_head_version)
        , editable(editable)
        , feature_dependencies(std::move(dependencies))
        , build_failure_messages(std::move(build_failure_messages))
        , package_dir(packages_dir_assigner.generate(spec))
    {
    }

    const std::string& InstallPlanAction::public_abi() const
    {
        switch (plan_type)
        {
            case InstallPlanType::ALREADY_INSTALLED:
                return installed_package.value_or_exit(VCPKG_LINE_INFO).core->package.abi;
            case InstallPlanType::BUILD_AND_INSTALL:
            {
                auto&& i = abi_info.value_or_exit(VCPKG_LINE_INFO);
                if (auto o = i.pre_build_info->public_abi_override.get())
                    return *o;
                else
                    return i.package_abi;
            }
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }
    bool InstallPlanAction::has_package_abi() const
    {
        const auto p = abi_info.get();
        return p && !p->package_abi.empty();
    }
    Optional<const std::string&> InstallPlanAction::package_abi() const
    {
        const auto p = abi_info.get();
        if (!p || p->package_abi.empty()) return nullopt;
        return p->package_abi;
    }
    const PreBuildInfo& InstallPlanAction::pre_build_info(LineInfo li) const
    {
        return *abi_info.value_or_exit(li).pre_build_info;
    }
    Version InstallPlanAction::version() const
    {
        if (auto scfl = source_control_file_and_location.get())
        {
            return scfl->to_version();
        }
        else if (auto ipv = installed_package.get())
        {
            return ipv->version();
        }
        else
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    std::string InstallPlanAction::display_name() const
    {
        auto version = this->version();
        if (this->feature_list.empty_or_only_core())
        {
            return fmt::format("{}@{}", this->spec.to_string(), version);
        }

        const std::string features = Strings::join(",", feature_list);
        return fmt::format("{}[{}]:{}@{}", this->spec.name(), features, this->spec.triplet(), version);
    }

    NotInstalledAction::NotInstalledAction(const PackageSpec& spec) : BasicAction{spec} { }

    RemovePlanAction::RemovePlanAction(const PackageSpec& spec, RequestType request_type)
        : BasicAction{spec}, request_type(request_type)
    {
    }

    static LocalizedString create_unsupported_message(decltype(msgUnsupportedFeatureSupportsExpression) m,
                                                      const FeatureSpec& spec,
                                                      const PlatformExpression::Expr& expr)
    {
        const auto feature_spec =
            (spec.feature() == FeatureNameCore ? spec.port()
                                               : format_name_only_feature_spec(spec.port(), spec.feature()));
        return msg::format(m,
                           msg::package_name = spec.port(),
                           msg::feature_spec = feature_spec,
                           msg::supports_expression = to_string(expr),
                           msg::triplet = spec.triplet());
    }

    void ActionPlan::print_unsupported_warnings()
    {
        for (const auto& entry : unsupported_features)
        {
            const auto& spec = entry.first;
            const auto feature_spec =
                (spec.feature() == FeatureNameCore ? spec.port()
                                                   : format_name_only_feature_spec(spec.port(), spec.feature()));
            msg::println_warning(msgUnsupportedFeatureSupportsExpressionWarning,
                                 msg::feature_spec = feature_spec,
                                 msg::supports_expression = to_string(entry.second),
                                 msg::triplet = spec.triplet());
        }
    }

    ExportPlanAction::ExportPlanAction(const PackageSpec& spec,
                                       InstalledPackageView&& installed_package,
                                       RequestType request_type)
        : BasicAction{spec}
        , plan_type(ExportPlanType::ALREADY_BUILT)
        , request_type(request_type)
        , m_installed_package(std::move(installed_package))
    {
    }

    ExportPlanAction::ExportPlanAction(const PackageSpec& spec, RequestType request_type)
        : BasicAction{spec}, plan_type(ExportPlanType::NOT_BUILT), request_type(request_type)
    {
    }

    Optional<const BinaryParagraph&> ExportPlanAction::core_paragraph() const
    {
        if (auto p_ip = m_installed_package.get())
        {
            return p_ip->core->package;
        }
        return nullopt;
    }

    std::vector<PackageSpec> ExportPlanAction::dependencies() const
    {
        if (auto p_ip = m_installed_package.get())
            return p_ip->dependencies();
        else
            return {};
    }

    bool RemovePlan::empty() const { return not_installed.empty() && remove.empty(); }

    bool RemovePlan::has_non_user_requested() const
    {
        return Util::any_of(remove,
                            [](const RemovePlanAction& a) { return a.request_type != RequestType::USER_REQUESTED; });
    }

    RemovePlan create_remove_plan(const std::vector<PackageSpec>& specs, const StatusParagraphs& status_db)
    {
        struct RemoveAdjacencyProvider final : AdjacencyProvider<PackageSpec, PackageSpec>
        {
            std::unordered_map<PackageSpec, std::vector<PackageSpec>> rev_edges;

            std::vector<PackageSpec> adjacency_list(const PackageSpec& spec) const override
            {
                return Util::copy_or_default(rev_edges, spec);
            }

            PackageSpec load_vertex_data(const PackageSpec& s) const override { return s; }
        };

        RemoveAdjacencyProvider p;
        for (auto&& a : get_installed_ports(status_db))
        {
            p.rev_edges.emplace(a.spec(), std::initializer_list<PackageSpec>{});
            for (auto&& b : a.dependencies())
            {
                p.rev_edges[b].push_back(a.spec());
            }
        }
        auto remove_order = topological_sort(specs, p, nullptr);

        const std::unordered_set<PackageSpec> requested(specs.cbegin(), specs.cend());
        RemovePlan plan;
        for (auto&& step : remove_order)
        {
            if (p.rev_edges.find(step) != p.rev_edges.end())
            {
                // installed
                plan.remove.emplace_back(step,
                                         Util::Sets::contains(requested, step) ? RequestType::USER_REQUESTED
                                                                               : RequestType::AUTO_SELECTED);
            }
            else
            {
                plan.not_installed.emplace_back(step);
            }
        }
        return plan;
    }

    std::vector<ExportPlanAction> create_export_plan(const std::vector<PackageSpec>& specs,
                                                     const StatusParagraphs& status_db)
    {
        struct ExportAdjacencyProvider final : AdjacencyProvider<PackageSpec, ExportPlanAction>
        {
            const StatusParagraphs& status_db;
            const std::unordered_set<PackageSpec>& specs_as_set;

            ExportAdjacencyProvider(const StatusParagraphs& s, const std::unordered_set<PackageSpec>& specs_as_set)
                : status_db(s), specs_as_set(specs_as_set)
            {
            }

            std::vector<PackageSpec> adjacency_list(const ExportPlanAction& plan) const override
            {
                return plan.dependencies();
            }

            ExportPlanAction load_vertex_data(const PackageSpec& spec) const override
            {
                const RequestType request_type =
                    Util::Sets::contains(specs_as_set, spec) ? RequestType::USER_REQUESTED : RequestType::AUTO_SELECTED;

                auto maybe_ipv = status_db.get_installed_package_view(spec);

                if (auto p_ipv = maybe_ipv.get())
                {
                    return ExportPlanAction{spec, std::move(*p_ipv), request_type};
                }

                return ExportPlanAction{spec, request_type};
            }
        };

        const std::unordered_set<PackageSpec> specs_as_set(specs.cbegin(), specs.cend());
        std::vector<ExportPlanAction> toposort =
            topological_sort(specs, ExportAdjacencyProvider{status_db, specs_as_set}, {});
        return toposort;
    }

    void PackageGraph::mark_user_requested(const PackageSpec& spec)
    {
        m_graph->get(spec).request_type = RequestType::USER_REQUESTED;
    }

    ActionPlan create_feature_install_plan(const PortFileProvider& port_provider,
                                           const CMakeVars::CMakeVarProvider& var_provider,
                                           View<FullPackageSpec> specs,
                                           const StatusParagraphs& status_db,
                                           PackagesDirAssigner& packages_dir_assigner,
                                           const CreateInstallPlanOptions& options)
    {
        PackageGraph pgraph(port_provider, var_provider, status_db, options.host_triplet, packages_dir_assigner);

        std::vector<FeatureSpec> feature_specs;
        for (const FullPackageSpec& spec : specs)
        {
            pgraph.mark_user_requested(spec.package_spec);
            spec.expand_fspecs_to(feature_specs);
        }
        Util::sort_unique_erase(feature_specs);

        pgraph.install(feature_specs, options.unsupported_port_action);

        return pgraph.serialize(
            options.randomizer, options.use_head_version_if_user_requested, options.editable_if_user_requested);
    }

    void PackageGraph::mark_for_reinstall(const PackageSpec& first_remove_spec,
                                          std::vector<FeatureSpec>& out_reinstall_requirements) const
    {
        std::set<PackageSpec> removed;
        std::vector<PackageSpec> to_remove{first_remove_spec};

        while (!to_remove.empty())
        {
            PackageSpec remove_spec = std::move(to_remove.back());
            to_remove.pop_back();

            if (!removed.insert(remove_spec).second) continue;

            Cluster& clust = m_graph->get(remove_spec);
            ClusterInstalled& info = clust.m_installed.value_or_exit(VCPKG_LINE_INFO);

            if (!clust.m_install_info)
            {
                clust.create_install_info(out_reinstall_requirements);
            }

            to_remove.insert(to_remove.end(), info.remove_edges.begin(), info.remove_edges.end());
        }
    }

    /// The list of specs to install should already have default features expanded
    void PackageGraph::install(Span<const FeatureSpec> specs, UnsupportedPortAction unsupported_port_action)
    {
        // We batch resolving qualified dependencies, because it's an invocation of CMake which
        // takes ~150ms per call.
        std::vector<FeatureSpec> qualified_dependencies;
        std::vector<FeatureSpec> next_dependencies{specs.begin(), specs.end()};

        // Keep running while there is any chance of finding more dependencies
        while (!next_dependencies.empty())
        {
            // Keep running until the only dependencies left are qualified
            while (!next_dependencies.empty())
            {
                // Extract the top of the stack
                const FeatureSpec spec = std::move(next_dependencies.back());
                next_dependencies.pop_back();

                // Get the cluster for the PackageSpec of the FeatureSpec we are adding to the install graph
                Cluster& clust = m_graph->get(spec.spec());

                if (spec.feature() == FeatureNameStar)
                {
                    // Expand wildcard feature
                    for (auto&& fpgh : clust.get_scfl_or_exit().source_control_file->feature_paragraphs)
                    {
                        next_dependencies.emplace_back(spec.spec(), fpgh->name);
                    }
                    continue;
                }

                // If this spec hasn't already had its qualified dependencies resolved
                if (!m_var_provider.get_dep_info_vars(spec.spec()).has_value())
                {
                    // TODO: There's always the chance that we don't find the feature we're looking for (probably a
                    // malformed CONTROL file somewhere). We should probably output a better error.
                    const std::vector<Dependency>* paragraph_depends = nullptr;
                    bool has_supports = false;
                    if (spec.feature() == FeatureNameCore)
                    {
                        paragraph_depends = &clust.get_scfl_or_exit().source_control_file->core_paragraph->dependencies;
                        has_supports = !clust.get_scfl_or_exit()
                                            .source_control_file->core_paragraph->supports_expression.is_empty();
                    }
                    else if (spec.feature() == FeatureNameDefault)
                    {
                        has_supports = Util::any_of(
                            clust.get_scfl_or_exit().source_control_file->core_paragraph->default_features,
                            [](const DependencyRequestedFeature& feature) { return !feature.platform.is_empty(); });
                    }
                    else
                    {
                        auto maybe_paragraph =
                            clust.get_scfl_or_exit().source_control_file->find_feature(spec.feature());
                        Checks::msg_check_maybe_upgrade(VCPKG_LINE_INFO,
                                                        maybe_paragraph.has_value(),
                                                        msgFailedToFindPortFeature,
                                                        msg::feature = spec.feature(),
                                                        msg::package_name = spec.port());
                        paragraph_depends = &maybe_paragraph.value_or_exit(VCPKG_LINE_INFO).dependencies;
                        has_supports = !maybe_paragraph.get()->supports_expression.is_empty();
                    }

                    // And it has at least one qualified dependency
                    if (has_supports ||
                        (paragraph_depends && Util::any_of(*paragraph_depends, [](const Dependency& dep) {
                             return dep.has_platform_expressions();
                         })))
                    {
                        // Add it to the next batch run
                        qualified_dependencies.emplace_back(spec);
                    }
                }
                else
                {
                    auto maybe_supports_expression = clust.get_applicable_supports_expression(spec);
                    auto supports_expression = maybe_supports_expression.get();
                    if (supports_expression && !supports_expression->is_empty())
                    {
                        if (!supports_expression->evaluate(
                                m_var_provider.get_dep_info_vars(spec.spec()).value_or_exit(VCPKG_LINE_INFO)))
                        {
                            const auto supports_expression_text = to_string(*supports_expression);
                            if (unsupported_port_action == UnsupportedPortAction::Error)
                            {
                                Checks::msg_exit_with_message(
                                    VCPKG_LINE_INFO,
                                    create_unsupported_message(
                                        msgUnsupportedFeatureSupportsExpression, spec, *supports_expression));
                            }
                            else
                            {
                                m_unsupported_features.emplace(spec, *supports_expression);
                            }
                        }
                    }
                }

                if (clust.m_install_info.has_value())
                {
                    clust.add_feature(spec.feature(), m_var_provider, next_dependencies, m_graph->m_host_triplet);
                }
                else
                {
                    if (!clust.m_installed.has_value())
                    {
                        clust.create_install_info(next_dependencies);
                        clust.add_feature(spec.feature(), m_var_provider, next_dependencies, m_graph->m_host_triplet);
                    }
                    else
                    {
                        if (spec.feature() == FeatureNameDefault)
                        {
                            if (!clust.m_installed.get()->defaults_requested)
                            {
                                clust.m_installed.get()->defaults_requested = true;
                                if (!clust.has_defaults_installed())
                                {
                                    mark_for_reinstall(spec.spec(), next_dependencies);
                                }
                            }
                        }
                        else if (!clust.has_feature_installed(spec.feature()))
                        {
                            // If install_info is not present and it is already installed, we have never added a feature
                            // which hasn't already been installed to this cluster. In this case, we need to reinstall
                            // the port if the feature isn't already present.
                            mark_for_reinstall(spec.spec(), next_dependencies);
                            clust.add_feature(
                                spec.feature(), m_var_provider, next_dependencies, m_graph->m_host_triplet);
                        }
                    }
                }
            }

            if (!qualified_dependencies.empty())
            {
                Util::sort_unique_erase(qualified_dependencies);

                // Extract the package specs we need to get dependency info from. We don't run the triplet on a per
                // feature basis. We run it once for the whole port.
                auto qualified_package_specs =
                    Util::fmap(qualified_dependencies, [](const FeatureSpec& fspec) { return fspec.spec(); });
                Util::sort_unique_erase(qualified_package_specs);
                m_var_provider.load_dep_info_vars(qualified_package_specs, m_graph->m_host_triplet);

                // Put all the FeatureSpecs for which we had qualified dependencies back on the dependencies stack.
                // We need to recheck if evaluating the triplet revealed any new dependencies.
                next_dependencies.insert(next_dependencies.end(),
                                         std::make_move_iterator(qualified_dependencies.begin()),
                                         std::make_move_iterator(qualified_dependencies.end()));
                qualified_dependencies.clear();
            }
        }
    }

    void PackageGraph::upgrade(Span<const PackageSpec> specs, UnsupportedPortAction unsupported_port_action)
    {
        std::vector<FeatureSpec> reinstall_reqs;

        for (const PackageSpec& spec : specs)
            mark_for_reinstall(spec, reinstall_reqs);

        Util::sort_unique_erase(reinstall_reqs);

        install(reinstall_reqs, unsupported_port_action);
    }

    ActionPlan create_upgrade_plan(const PortFileProvider& port_provider,
                                   const CMakeVars::CMakeVarProvider& var_provider,
                                   const std::vector<PackageSpec>& specs,
                                   const StatusParagraphs& status_db,
                                   PackagesDirAssigner& packages_dir_assigner,
                                   const CreateUpgradePlanOptions& options)
    {
        PackageGraph pgraph(port_provider, var_provider, status_db, options.host_triplet, packages_dir_assigner);

        pgraph.upgrade(specs, options.unsupported_port_action);

        return pgraph.serialize(options.randomizer, UseHeadVersion::No, Editable::No);
    }

    ActionPlan PackageGraph::serialize(GraphRandomizer* randomizer,
                                       UseHeadVersion use_head_version_if_user_requested,
                                       Editable editable_if_user_requested) const
    {
        struct BaseEdgeProvider : AdjacencyProvider<PackageSpec, const Cluster*>
        {
            BaseEdgeProvider(const ClusterGraph& parent) : m_parent(parent) { }

            const Cluster* load_vertex_data(const PackageSpec& spec) const override
            {
                return &m_parent.find_or_exit(spec, VCPKG_LINE_INFO);
            }

            const ClusterGraph& m_parent;
        };

        struct RemoveEdgeProvider final : BaseEdgeProvider
        {
            using BaseEdgeProvider::BaseEdgeProvider;

            std::vector<PackageSpec> adjacency_list(const Cluster* const& vertex) const override
            {
                auto&& set = vertex->m_installed.value_or_exit(VCPKG_LINE_INFO).remove_edges;
                return {set.begin(), set.end()};
            }
        } removeedgeprovider(*m_graph);

        struct InstallEdgeProvider final : BaseEdgeProvider
        {
            using BaseEdgeProvider::BaseEdgeProvider;

            std::vector<PackageSpec> adjacency_list(const Cluster* const& vertex) const override
            {
                if (!vertex->m_install_info.has_value()) return {};

                auto& info = vertex->m_install_info.value_or_exit(VCPKG_LINE_INFO);
                std::vector<PackageSpec> deps;
                for (auto&& kv : info.build_edges)
                    for (auto&& e : kv.second)
                    {
                        auto spec = e.spec();
                        if (spec != vertex->m_spec) deps.push_back(std::move(spec));
                    }
                Util::sort_unique_erase(deps);
                return deps;
            }
        } installedgeprovider(*m_graph);

        std::vector<PackageSpec> removed_vertices;
        std::vector<PackageSpec> installed_vertices;
        for (auto&& kv : *m_graph)
        {
            if (kv.second.m_install_info.has_value() && kv.second.m_installed.has_value())
            {
                removed_vertices.push_back(kv.first);
            }
            if (kv.second.m_install_info.has_value() || kv.second.request_type == RequestType::USER_REQUESTED)
            {
                installed_vertices.push_back(kv.first);
            }
        }
        auto remove_toposort = topological_sort(removed_vertices, removeedgeprovider, randomizer);
        auto insert_toposort = topological_sort(installed_vertices, installedgeprovider, randomizer);

        ActionPlan plan;

        for (const auto* p_cluster : remove_toposort)
        {
            plan.remove_actions.emplace_back(p_cluster->m_spec, p_cluster->request_type);
        }

        for (const auto* p_cluster : insert_toposort)
        {
            // Every cluster that has an install_info needs to be built
            // If a cluster only has an installed object and is marked as user requested we should still report it.
            if (auto info_ptr = p_cluster->m_install_info.get())
            {
                std::vector<LocalizedString> constraint_violations;
                for (auto&& constraints : info_ptr->version_constraints)
                {
                    for (auto&& constraint : constraints.second)
                    {
                        auto&& dep_clust = m_graph->get(constraints.first);
                        auto maybe_v = dep_clust.get_version();
                        if (auto v = maybe_v.get())
                        {
                            if (compare_any(*v, constraint) == VerComp::lt)
                            {
                                constraint_violations.push_back(msg::format_warning(msgVersionConstraintViolated,
                                                                                    msg::spec = constraints.first,
                                                                                    msg::expected_version = constraint,
                                                                                    msg::actual_version = *v));
                                msg::println(msg::format(msgConstraintViolation)
                                                 .append_raw('\n')
                                                 .append_indent()
                                                 .append(constraint_violations.back()));
                            }
                        }
                    }
                }
                std::map<std::string, std::vector<FeatureSpec>> computed_edges;
                for (auto&& kv : info_ptr->build_edges)
                {
                    std::set<FeatureSpec> fspecs;
                    for (auto&& fspec : kv.second)
                    {
                        if (fspec.feature() != FeatureNameDefault)
                        {
                            fspecs.insert(fspec);
                            continue;
                        }

                        auto&& dep_clust = m_graph->get(fspec.spec());
                        const auto& default_features = [&] {
                            if (dep_clust.m_install_info.has_value())
                            {
                                return dep_clust.m_install_info.get()->default_features;
                            }

                            if (auto p = dep_clust.m_installed.get())
                            {
                                return p->ipv.core->package.default_features;
                            }

                            Checks::unreachable(VCPKG_LINE_INFO);
                        }();

                        for (auto&& default_feature : default_features)
                        {
                            fspecs.emplace(fspec.spec(), default_feature);
                        }
                    }
                    computed_edges[kv.first].assign(fspecs.begin(), fspecs.end());
                }

                UseHeadVersion use_head_version;
                Editable editable;
                if (p_cluster->request_type == RequestType::USER_REQUESTED)
                {
                    use_head_version = use_head_version_if_user_requested;
                    editable = editable_if_user_requested;
                }
                else
                {
                    use_head_version = UseHeadVersion::No;
                    editable = Editable::No;
                }

                plan.install_actions.emplace_back(p_cluster->m_spec,
                                                  p_cluster->get_scfl_or_exit(),
                                                  m_packages_dir_assigner,
                                                  p_cluster->request_type,
                                                  use_head_version,
                                                  editable,
                                                  std::move(computed_edges),
                                                  std::move(constraint_violations),
                                                  info_ptr->default_features);
            }
            else if (p_cluster->request_type == RequestType::USER_REQUESTED && p_cluster->m_installed.has_value())
            {
                auto&& installed = p_cluster->m_installed.value_or_exit(VCPKG_LINE_INFO);
                plan.already_installed.emplace_back(InstalledPackageView(installed.ipv),
                                                    p_cluster->request_type,
                                                    use_head_version_if_user_requested,
                                                    editable_if_user_requested);
            }
        }
        plan.unsupported_features = m_unsupported_features;
        return plan;
    }

    static std::unique_ptr<ClusterGraph> create_feature_install_graph(const PortFileProvider& port_provider,
                                                                      const StatusParagraphs& status_db,
                                                                      Triplet host_triplet)
    {
        std::unique_ptr<ClusterGraph> graph = std::make_unique<ClusterGraph>(port_provider, host_triplet);

        auto installed_ports = get_installed_ports(status_db);

        for (auto&& ipv : installed_ports)
        {
            graph->insert(ipv);
        }

        // Populate the graph with "remove edges", which are the reverse of the Build-Depends edges.
        for (auto&& ipv : installed_ports)
        {
            auto deps = ipv.dependencies();

            for (auto&& dep : deps)
            {
                auto p_installed = graph->get(dep).m_installed.get();
                if (p_installed == nullptr)
                {
                    Checks::msg_exit_with_error(
                        VCPKG_LINE_INFO,
                        msg::format(msgCorruptedDatabase)
                            .append_raw('\n')
                            .append(msgMissingDependency, msg::spec = ipv.spec(), msg::package_name = dep));
                }

                p_installed->remove_edges.emplace(ipv.spec());
            }
        }
        return graph;
    }

    PackageGraph::PackageGraph(const PortFileProvider& port_provider,
                               const CMakeVars::CMakeVarProvider& var_provider,
                               const StatusParagraphs& status_db,
                               Triplet host_triplet,
                               PackagesDirAssigner& packages_dir_assigner)
        : m_var_provider(var_provider)
        , m_graph(create_feature_install_graph(port_provider, status_db, host_triplet))
        , m_packages_dir_assigner(packages_dir_assigner)
    {
    }

    static void format_plan_block(LocalizedString& msg,
                                  msg::MessageT<> header,
                                  bool add_head_tag,
                                  View<const InstallPlanAction*> actions)
    {
        msg.append(header).append_raw('\n');
        for (auto action : actions)
        {
            format_plan_ipa_row(msg, add_head_tag, *action);
            msg.append_raw('\n');
        }
    }

    static void format_plan_block(LocalizedString& msg, msg::MessageT<> header, const std::set<PackageSpec>& specs)
    {
        msg.append(header).append_raw('\n');
        for (auto&& spec : specs)
        {
            msg.append_raw(request_type_indent(RequestType::USER_REQUESTED)).append_raw(spec).append_raw('\n');
        }
    }

    LocalizedString FormattedPlan::all_text() const
    {
        auto result = warning_text;
        result.append(normal_text);
        return result;
    }

    FormattedPlan format_plan(const ActionPlan& action_plan)
    {
        FormattedPlan ret;
        if (action_plan.remove_actions.empty() && action_plan.already_installed.empty() &&
            action_plan.install_actions.empty())
        {
            ret.normal_text = msg::format(msgInstalledRequestedPackages);
            ret.normal_text.append_raw('\n');
            return ret;
        }

        std::set<PackageSpec> remove_specs;
        std::vector<const InstallPlanAction*> rebuilt_plans;
        std::vector<const InstallPlanAction*> new_plans;
        std::vector<const InstallPlanAction*> already_installed_plans;
        std::vector<const InstallPlanAction*> already_installed_head_plans;
        std::vector<const InstallPlanAction*> excluded;

        const bool has_non_user_requested_packages =
            Util::any_of(action_plan.install_actions, [](const InstallPlanAction& action) -> bool {
                return action.request_type != RequestType::USER_REQUESTED;
            });

        for (auto&& already_installed_action : action_plan.already_installed)
        {
            (already_installed_action.use_head_version == UseHeadVersion::Yes ? &already_installed_head_plans
                                                                              : &already_installed_plans)
                ->push_back(&already_installed_action);
        }

        for (auto&& remove_action : action_plan.remove_actions)
        {
            remove_specs.emplace(remove_action.spec);
        }

        for (auto&& install_action : action_plan.install_actions)
        {
            // remove plans are guaranteed to come before install plans, so we know the plan will be contained
            // if at all.
            auto it = remove_specs.find(install_action.spec);
            if (it == remove_specs.end())
            {
                (install_action.plan_type == InstallPlanType::EXCLUDED ? &excluded : &new_plans)
                    ->push_back(&install_action);
            }
            else
            {
                remove_specs.erase(it);
                rebuilt_plans.push_back(&install_action);
            }
        }

        Util::sort(rebuilt_plans, &InstallPlanAction::compare_by_name);
        Util::sort(new_plans, &InstallPlanAction::compare_by_name);
        Util::sort(already_installed_plans, &InstallPlanAction::compare_by_name);
        Util::sort(already_installed_head_plans, &InstallPlanAction::compare_by_name);
        Util::sort(excluded, &InstallPlanAction::compare_by_name);

        if (!excluded.empty())
        {
            format_plan_block(ret.warning_text, msgExcludedPackages, false, excluded);
        }

        if (!already_installed_head_plans.empty())
        {
            format_plan_block(ret.warning_text, msgInstalledPackagesHead, false, already_installed_head_plans);
        }

        if (!already_installed_plans.empty())
        {
            format_plan_block(ret.normal_text, msgInstalledPackages, false, already_installed_plans);
        }

        if (!remove_specs.empty())
        {
            format_plan_block(ret.normal_text, msgPackagesToRemove, remove_specs);
        }

        if (!rebuilt_plans.empty())
        {
            format_plan_block(ret.normal_text, msgPackagesToRebuild, true, rebuilt_plans);
        }

        if (!new_plans.empty())
        {
            format_plan_block(ret.normal_text, msgPackagesToInstall, true, new_plans);
        }

        if (has_non_user_requested_packages)
        {
            ret.normal_text.append(msgPackagesToModify).append_raw('\n');
        }

        ret.has_removals = !remove_specs.empty() || !rebuilt_plans.empty();
        return ret;
    }

    FormattedPlan print_plan(const ActionPlan& action_plan)
    {
        auto formatted = format_plan(action_plan);
        if (!formatted.warning_text.empty())
        {
            msg::print(Color::warning, formatted.warning_text);
        }

        msg::print(formatted.normal_text);
        return formatted;
    }

    namespace
    {

        /**
         * vcpkg's Versioned Constraint Resolution Algorithm
         * ---
         *
         * Phase 1:
         * - Every spec not mentioned at top-level will have default features applied.
         * - Every feature constraint from all applied versions will be applied.
         * - If pinned, that version will be applied; otherwise the baseline version will be applied.
         * - If a spec is not pinned, and a version constraint compares >= the baseline, that version will be applied.
         *
         * Phase 2:
         * - Perform a postfix walk to serialize the plan.
         *   - Use the greatest version applied from Phase 1.
         *   - Use all features applied in Phase 1 that exist in the selected version.
         *   - Validate that every version constraint from the selected version is satisfied or pinned.
         *   - Validate that every feature constraint from the selected version is satisfied.
         * - Validate that every spec in the plan is supported, applying the user's policy.
         * - Validate that every feature in the plan is supported, applying the user's policy.
         *
         * (pinned means there is a matching override or overlay)
         *
         * Phase 1 does not depend on the order of evaluation. The implementation below exploits this to batch calls to
         * CMake for calculating dependency resolution tags. However, the results are sensitive to the definition of
         * comparison. If "compares >= the baseline" changes, the set of considered constraints will change, and so will
         * the results.
         */

        struct VersionedPackageGraph
        {
            VersionedPackageGraph(const IVersionedPortfileProvider& ver_provider,
                                  const IBaselineProvider& base_provider,
                                  const IOverlayProvider& oprovider,
                                  const CMakeVars::CMakeVarProvider& var_provider,
                                  const PackageSpec& toplevel,
                                  Triplet host_triplet,
                                  PackagesDirAssigner& packages_dir_assigner)
                : m_ver_provider(ver_provider)
                , m_base_provider(base_provider)
                , m_o_provider(oprovider)
                , m_var_provider(var_provider)
                , m_toplevel(toplevel)
                , m_host_triplet(host_triplet)
                , m_packages_dir_assigner(packages_dir_assigner)
            {
            }

            void add_override(const std::string& name, const Version& v);

            void solve_with_roots(View<Dependency> dep);

            ExpectedL<ActionPlan> finalize_extract_plan(UnsupportedPortAction unsupported_port_action,
                                                        UseHeadVersion use_head_version_if_user_requested,
                                                        Editable editable_if_user_requested);

        private:
            const IVersionedPortfileProvider& m_ver_provider;
            const IBaselineProvider& m_base_provider;
            const IOverlayProvider& m_o_provider;
            const CMakeVars::CMakeVarProvider& m_var_provider;
            const PackageSpec& m_toplevel;
            const Triplet m_host_triplet;
            PackagesDirAssigner& m_packages_dir_assigner;

            struct DepSpec
            {
                PackageSpec spec;
                DependencyConstraint dc;
                std::vector<DependencyRequestedFeature> features;
            };

            struct PackageNodeData
            {
                // set of all scfls that have been considered
                std::set<const SourceControlFileAndLocation*> considered;

                // Versions occluded by the baseline constraint are not considered.
                SchemedVersion baseline;
                // If overlay_or_override is true, ignore scheme and baseline_version
                bool overlay_or_override = false;
                // The current "best" scfl
                const SourceControlFileAndLocation* scfl = nullptr;

                // This tracks a list of constraint sources for debugging purposes
                std::set<std::string> origins;

                // The set of features that have been requested across all constraints
                std::set<std::string> requested_features;
                bool default_features = false;
            };

            using PackageNode = std::pair<const PackageSpec, PackageNodeData>;

            // mapping from portname -> version. "overrides" field in manifest file
            std::map<std::string, Version> m_overrides;
            // direct dependencies in unevaluated form
            std::vector<DepSpec> m_roots;
            // set of direct dependencies
            std::set<PackageSpec> m_user_requested;
            // mapping from { package specifier -> node containing resolution information for that package }
            std::map<PackageSpec, PackageNodeData> m_graph;
            // the set of nodes that could not be constructed in the graph due to failures
            std::set<std::string> m_failed_nodes;

            struct ConstraintFrame
            {
                PackageSpec spec;
                View<Dependency> deps;
            };
            std::vector<ConstraintFrame> m_resolve_stack;

            // Add an initial requirement for a package.
            // Returns a reference to the node to place additional constraints
            Optional<PackageNode&> require_package(const PackageSpec& spec, const std::string& origin);

            void require_scfl(PackageNode& ref, const SourceControlFileAndLocation* scfl, const std::string& origin);

            void require_port_feature(PackageNode& ref, const std::string& feature, const std::string& origin);

            void require_port_defaults(PackageNode& ref, const std::string& origin);

            void resolve_stack(const ConstraintFrame& frame);
            const CMakeVars::CMakeVars& batch_load_vars(const PackageSpec& spec);

            Optional<const PackageNode&> find_package(const PackageSpec& spec) const;

            // For node, for each requested feature existing in the best scfl, calculate the set of package and feature
            // dependencies.
            // The FeatureSpec list will contain a [core] entry for each package dependency.
            // The FeatureSpec list will not contain [default].
            std::map<std::string, std::vector<FeatureSpec>> compute_feature_dependencies(
                const PackageNode& node, std::vector<DepSpec>& out_dep_specs) const;

            bool evaluate(const PackageSpec& spec, const PlatformExpression::Expr& platform_expr) const;

            static LocalizedString format_incomparable_versions_message(const PackageSpec& on,
                                                                        StringView from,
                                                                        const SchemedVersion& baseline,
                                                                        const SchemedVersion& target);
            std::vector<LocalizedString> m_errors;
        };

        const CMakeVars::CMakeVars& VersionedPackageGraph::batch_load_vars(const PackageSpec& spec)
        {
            auto vars = m_var_provider.get_dep_info_vars(spec);
            if (!vars)
            {
                // We want to batch as many dep_infos as possible, so look ahead in the stack
                std::unordered_set<PackageSpec> spec_set = {spec};
                for (auto&& s : m_resolve_stack)
                {
                    spec_set.insert(s.spec);
                    for (auto&& d : s.deps)
                        spec_set.emplace(d.name, d.host ? m_host_triplet : s.spec.triplet());
                }
                std::vector<PackageSpec> spec_vec(spec_set.begin(), spec_set.end());
                m_var_provider.load_dep_info_vars(spec_vec, m_host_triplet);
                return m_var_provider.get_dep_info_vars(spec).value_or_exit(VCPKG_LINE_INFO);
            }
            return *vars.get();
        }

        void VersionedPackageGraph::resolve_stack(const ConstraintFrame& frame)
        {
            for (auto&& dep : frame.deps)
            {
                if (!dep.platform.is_empty() && !dep.platform.evaluate(batch_load_vars(frame.spec))) continue;

                PackageSpec dep_spec(dep.name, dep.host ? m_host_triplet : frame.spec.triplet());
                auto maybe_node = require_package(dep_spec, frame.spec.name());
                if (auto node = maybe_node.get())
                {
                    // If the node is overlayed or overridden, don't apply version constraints
                    // If the baseline is a version_string, it occludes other constraints
                    if (!node->second.overlay_or_override)
                    {
                        const auto maybe_dep_ver = dep.constraint.try_get_minimum_version();
                        if (auto dep_ver = maybe_dep_ver.get())
                        {
                            auto maybe_scfl = m_ver_provider.get_control_file({dep.name, *dep_ver});
                            if (auto p_scfl = maybe_scfl.get())
                            {
                                const auto sver = p_scfl->schemed_version();
                                if (compare_versions(node->second.scfl->schemed_version(), sver) == VerComp::lt)
                                {
                                    // mark as current best and apply constraints
                                    node->second.scfl = p_scfl;
                                    require_scfl(*node, p_scfl, frame.spec.name());
                                }
                                else if (compare_versions(node->second.baseline, sver) == VerComp::lt)
                                {
                                    // apply constraints
                                    require_scfl(*node, p_scfl, frame.spec.name());
                                }
                            }
                        }
                    }

                    // apply selected features
                    for (auto&& f : dep.features)
                    {
                        if (f.name == FeatureNameDefault) abort();
                        if (evaluate(frame.spec, f.platform))
                        {
                            require_port_feature(*node, f.name, frame.spec.name());
                        }
                    }
                    if (dep.default_features)
                    {
                        require_port_defaults(*node, frame.spec.name());
                    }
                }
            }
        }

        void VersionedPackageGraph::require_port_defaults(PackageNode& ref, const std::string& origin)
        {
            ref.second.origins.insert(origin);
            if (!ref.second.default_features)
            {
                ref.second.default_features = true;

                auto scfls = ref.second.considered;
                for (auto scfl : scfls)
                {
                    for (auto&& f : scfl->source_control_file->core_paragraph->default_features)
                    {
                        if (evaluate(ref.first, f.platform))
                        {
                            auto deps = scfl->source_control_file->find_dependencies_for_feature(f.name);
                            if (!deps) continue;
                            m_resolve_stack.push_back({ref.first, *deps.get()});
                        }
                    }
                }
            }
        }
        void VersionedPackageGraph::require_port_feature(PackageNode& ref,
                                                         const std::string& feature,
                                                         const std::string& origin)
        {
            ref.second.origins.insert(origin);
            auto inserted = ref.second.requested_features.emplace(feature).second;
            if (inserted)
            {
                auto scfls = ref.second.considered;
                for (auto scfl : scfls)
                {
                    auto deps = scfl->source_control_file->find_dependencies_for_feature(feature);
                    if (!deps) continue;
                    m_resolve_stack.push_back({ref.first, *deps.get()});
                }
            }
        }
        void VersionedPackageGraph::require_scfl(PackageNode& ref,
                                                 const SourceControlFileAndLocation* scfl,
                                                 const std::string& origin)
        {
            ref.second.origins.insert(origin);

            if (Util::Sets::contains(ref.second.considered, scfl)) return;
            ref.second.considered.insert(scfl);

            auto features = ref.second.requested_features;
            if (ref.second.default_features)
            {
                for (auto&& f : ref.second.scfl->source_control_file->core_paragraph->default_features)
                {
                    if (evaluate(ref.first, f.platform))
                    {
                        features.insert(f.name);
                    }
                }
            }

            m_resolve_stack.push_back({ref.first, scfl->source_control_file->core_paragraph->dependencies});
            for (auto&& f : features)
            {
                auto deps = ref.second.scfl->source_control_file->find_dependencies_for_feature(f);
                if (!deps)
                {
                    // This version doesn't have this feature.
                    return;
                }
                m_resolve_stack.push_back({ref.first, *deps.get()});
            }
        }

        Optional<const VersionedPackageGraph::PackageNode&> VersionedPackageGraph::find_package(
            const PackageSpec& spec) const
        {
            auto it = m_graph.find(spec);
            if (it == m_graph.end()) return nullopt;
            return *it;
        }

        Optional<VersionedPackageGraph::PackageNode&> VersionedPackageGraph::require_package(const PackageSpec& spec,
                                                                                             const std::string& origin)
        {
            // Implicit defaults are disabled if spec is requested from top-level spec.
            const bool default_features_mask = origin != m_toplevel.name();

            auto it = m_graph.find(spec);
            if (it != m_graph.end())
            {
                it->second.origins.insert(origin);
                it->second.default_features &= default_features_mask;
                return *it;
            }

            if (Util::Maps::contains(m_failed_nodes, spec.name()))
            {
                return nullopt;
            }

            const auto maybe_overlay = m_o_provider.get_control_file(spec.name());
            if (auto p_overlay = maybe_overlay.get())
            {
                it = m_graph.emplace(spec, PackageNodeData{}).first;
                it->second.overlay_or_override = true;
                it->second.scfl = p_overlay;
            }
            else
            {
                Version ver;
                if (const auto over_it = m_overrides.find(spec.name()); over_it != m_overrides.end())
                {
                    auto maybe_scfl = m_ver_provider.get_control_file({spec.name(), over_it->second});
                    if (auto p_scfl = maybe_scfl.get())
                    {
                        it = m_graph.emplace(spec, PackageNodeData{}).first;
                        it->second.overlay_or_override = true;
                        it->second.scfl = p_scfl;
                    }
                    else
                    {
                        m_errors.push_back(std::move(maybe_scfl).error());
                        m_failed_nodes.insert(spec.name());
                        return nullopt;
                    }
                }
                else
                {
                    auto maybe_scfl = m_base_provider.get_baseline_version(spec.name()).then([&](const Version& ver) {
                        return m_ver_provider.get_control_file({spec.name(), ver});
                    });
                    if (auto p_scfl = maybe_scfl.get())
                    {
                        it = m_graph.emplace(spec, PackageNodeData{}).first;
                        it->second.baseline = p_scfl->schemed_version();
                        it->second.scfl = p_scfl;
                    }
                    else
                    {
                        m_errors.push_back(std::move(maybe_scfl).error());
                        m_failed_nodes.insert(spec.name());
                        return nullopt;
                    }
                }
            }

            it->second.default_features = default_features_mask;
            // Note that if top-level doesn't also mark that reference as `[core]`, defaults will be re-engaged.
            it->second.requested_features.insert(FeatureNameCore.to_string());

            require_scfl(*it, it->second.scfl, origin);
            return *it;
        }

        bool VersionedPackageGraph::evaluate(const PackageSpec& spec,
                                             const PlatformExpression::Expr& platform_expr) const
        {
            return platform_expr.evaluate(m_var_provider.get_or_load_dep_info_vars(spec, m_host_triplet));
        }

        void VersionedPackageGraph::add_override(const std::string& name, const Version& v)
        {
            m_overrides.emplace(name, v);
        }

        void VersionedPackageGraph::solve_with_roots(View<Dependency> deps)
        {
            auto dep_to_spec = [this](const Dependency& d) {
                return PackageSpec{d.name, d.host ? m_host_triplet : m_toplevel.triplet()};
            };
            auto specs = Util::fmap(deps, dep_to_spec);

            specs.push_back(m_toplevel);
            Util::sort_unique_erase(specs);
            for (auto&& dep : deps)
            {
                if (!dep.platform.is_empty() &&
                    !dep.platform.evaluate(m_var_provider.get_or_load_dep_info_vars(m_toplevel, m_host_triplet)))
                {
                    continue;
                }

                auto spec = dep_to_spec(dep);
                m_user_requested.insert(spec);
                m_roots.push_back(DepSpec{std::move(spec), dep.constraint, dep.features});
            }

            m_resolve_stack.push_back({m_toplevel, deps});

            while (!m_resolve_stack.empty())
            {
                ConstraintFrame frame = std::move(m_resolve_stack.back());
                m_resolve_stack.pop_back();
                // Frame must be passed as a local because resolve_stack() will add new elements to m_resolve_stack
                resolve_stack(frame);
            }
        }

        LocalizedString VersionedPackageGraph::format_incomparable_versions_message(const PackageSpec& on,
                                                                                    StringView from,
                                                                                    const SchemedVersion& baseline,
                                                                                    const SchemedVersion& target)
        {
            LocalizedString doc = msg::format_error(msgVersionIncomparable1,
                                                    msg::spec = on,
                                                    msg::constraint_origin = from,
                                                    msg::expected = target.version,
                                                    msg::actual = baseline.version)
                                      .append_raw("\n\n");
            if (baseline.scheme == VersionScheme::String && target.scheme == VersionScheme::String)
            {
                doc.append(msgVersionIncomparableSchemeString).append_raw("\n\n");
            }
            else
            {
                doc.append(msgVersionIncomparableSchemes).append_raw('\n');
                doc.append_indent()
                    .append(msgVersionIncomparable2,
                            msg::version_spec = Strings::concat(on.name(), '@', baseline.version),
                            msg::new_scheme = baseline.scheme)
                    .append_raw('\n');
                doc.append_indent()
                    .append(msgVersionIncomparable2,
                            msg::version_spec = Strings::concat(on.name(), '@', target.version),
                            msg::new_scheme = target.scheme)
                    .append_raw("\n\n");
            }
            doc.append(msgVersionIncomparable3).append_raw("\n");

            Json::Array example_array;
            serialize_dependency_override(example_array, DependencyOverride{on.name(), baseline.version});
            doc.append_raw(Json::stringify_object_member(OVERRIDES, example_array, Json::JsonStyle::with_spaces(2), 1));

            doc.append(msgVersionIncomparable4, msg::url = docs::troubleshoot_versioning_url);
            return doc;
        }

        std::map<std::string, std::vector<FeatureSpec>> VersionedPackageGraph::compute_feature_dependencies(
            const PackageNode& node, std::vector<DepSpec>& out_dep_specs) const
        {
            std::map<std::string, std::vector<FeatureSpec>> feature_deps;
            std::set<std::string> all_features = node.second.requested_features;
            if (node.second.default_features)
            {
                for (auto&& f : node.second.scfl->source_control_file->core_paragraph->default_features)
                {
                    if (evaluate(node.first, f.platform))
                    {
                        all_features.insert(f.name);
                    }
                }
            }
            std::vector<FeatureSpec> fspecs;
            for (auto&& f : all_features)
            {
                auto maybe_fdeps = node.second.scfl->source_control_file->find_dependencies_for_feature(f);
                if (auto fdeps = maybe_fdeps.get())
                {
                    fspecs.clear();
                    for (auto&& fdep : *fdeps)
                    {
                        PackageSpec fspec{fdep.name, fdep.host ? m_host_triplet : node.first.triplet()};

                        // Ignore intra-package dependencies
                        if (fspec == node.first) continue;

                        if (!fdep.platform.is_empty() &&
                            !fdep.platform.evaluate(
                                m_var_provider.get_or_load_dep_info_vars(node.first, m_host_triplet)))
                        {
                            continue;
                        }

                        fspecs.emplace_back(fspec, FeatureNameCore);
                        for (auto&& g : fdep.features)
                        {
                            if (evaluate(fspec, g.platform))
                            {
                                fspecs.emplace_back(fspec, g.name);
                            }
                        }
                        out_dep_specs.push_back({std::move(fspec), fdep.constraint, fdep.features});
                    }
                    Util::sort_unique_erase(fspecs);
                    feature_deps.emplace(f, fspecs);
                }
            }
            return feature_deps;
        }

        // This function is called after all versioning constraints have been resolved. It is responsible for
        // serializing out the final execution graph and performing all final validations.
        ExpectedL<ActionPlan> VersionedPackageGraph::finalize_extract_plan(
            UnsupportedPortAction unsupported_port_action,
            UseHeadVersion use_head_version_if_user_requested,
            Editable editable_if_user_requested)
        {
            if (!m_errors.empty())
            {
                Util::sort_unique_erase(m_errors);
                return LocalizedString::from_raw(Strings::join("\n", m_errors));
            }

            ActionPlan ret;

            // second == false means "in progress"
            std::map<PackageSpec, bool> emitted;
            struct Frame
            {
                InstallPlanAction ipa;
                std::vector<DepSpec> deps;
            };
            std::vector<Frame> stack;

            // Adds a new Frame to the stack if the spec was not already added
            auto push = [&emitted, this, &stack, use_head_version_if_user_requested, editable_if_user_requested](
                            const DepSpec& dep, StringView origin) -> ExpectedL<Unit> {
                auto p = emitted.emplace(dep.spec, false);
                // Dependency resolution should have ensured that either every node exists OR an error should have been
                // logged to m_errors
                const auto& node = find_package(dep.spec).value_or_exit(VCPKG_LINE_INFO);

                // Evaluate the >=version constraint (if any)
                auto maybe_min = dep.dc.try_get_minimum_version();
                if (!node.second.overlay_or_override && maybe_min)
                {
                    // Dependency resolution should have already logged any errors retrieving the scfl
                    const auto& dep_scfl = m_ver_provider.get_control_file({dep.spec.name(), *maybe_min.get()})
                                               .value_or_exit(VCPKG_LINE_INFO);
                    const auto constraint_sver = dep_scfl.schemed_version();
                    const auto selected_sver = node.second.scfl->schemed_version();
                    auto r = compare_versions(selected_sver, constraint_sver);
                    if (r == VerComp::unk)
                    {
                        // In the error message, we report the baseline version instead of the "best selected" version
                        // to give the user simpler data to work with.
                        return format_incomparable_versions_message(
                            dep.spec, origin, node.second.baseline, constraint_sver);
                    }
                    Checks::check_exit(
                        VCPKG_LINE_INFO,
                        r != VerComp::lt,
                        "Dependency resolution failed to consider a constraint. This is an internal error.");
                }

                // Evaluate feature constraints (if any)
                for (auto&& f : dep.features)
                {
                    if (f.name == FeatureNameCore) continue;
                    if (f.name == FeatureNameDefault) continue;
                    auto feature = node.second.scfl->source_control_file->find_feature(f.name);
                    if (!feature)
                    {
                        return msg::format_error(
                            msgVersionMissingRequiredFeature,
                            msg::version_spec = Strings::concat(dep.spec.name(), '@', node.second.scfl->to_version()),
                            msg::feature = f.name,
                            msg::constraint_origin = origin);
                    }
                }

                if (p.second)
                {
                    // Newly inserted -> Add stack frame
                    auto maybe_vars = m_var_provider.get_or_load_dep_info_vars(p.first->first, m_host_triplet);

                    std::vector<std::string> default_features;
                    for (const auto& feature : node.second.scfl->source_control_file->core_paragraph->default_features)
                    {
                        if (feature.platform.evaluate(maybe_vars))
                        {
                            default_features.push_back(feature.name);
                        }
                    }
                    std::vector<DepSpec> deps;
                    RequestType request;
                    UseHeadVersion use_head_version;
                    Editable editable;
                    if (Util::Sets::contains(m_user_requested, dep.spec))
                    {
                        request = RequestType::USER_REQUESTED;
                        use_head_version = use_head_version_if_user_requested;
                        editable = editable_if_user_requested;
                    }
                    else
                    {
                        request = RequestType::AUTO_SELECTED;
                        use_head_version = UseHeadVersion::No;
                        editable = Editable::No;
                    }

                    InstallPlanAction ipa(dep.spec,
                                          *node.second.scfl,
                                          m_packages_dir_assigner,
                                          request,
                                          use_head_version,
                                          editable,
                                          compute_feature_dependencies(node, deps),
                                          {},
                                          std::move(default_features));
                    stack.push_back(Frame{std::move(ipa), std::move(deps)});
                }
                else if (p.first->second == false)
                {
                    return msg::format_error(msgCycleDetectedDuring, msg::spec = dep.spec)
                        .append_raw('\n')
                        .append_raw(Strings::join("\n", stack, [](const Frame& p) {
                            return Strings::concat(
                                p.ipa.spec,
                                '@',
                                p.ipa.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).to_version());
                        }));
                }
                return Unit{};
            };

            for (auto&& root : m_roots)
            {
                auto x = push(root, m_toplevel.name());
                if (!x.has_value())
                {
                    return std::move(x).error();
                }

                while (!stack.empty())
                {
                    auto& back = stack.back();
                    if (back.deps.empty())
                    {
                        emitted[back.ipa.spec] = true;
                        ret.install_actions.push_back(std::move(back.ipa));
                        stack.pop_back();
                    }
                    else
                    {
                        auto dep = std::move(back.deps.back());
                        back.deps.pop_back();
                        const auto origin = Strings::concat(
                            back.ipa.spec,
                            "@",
                            back.ipa.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).to_version());
                        x = push(dep, origin);
                        if (!x.has_value())
                        {
                            return std::move(x).error();
                        }
                    }
                }
            }

            // Because supports expressions are commonplace, we assume that all dep info will be needed
            m_var_provider.load_dep_info_vars(
                Util::fmap(ret.install_actions, [](const InstallPlanAction& a) { return a.spec; }), m_host_triplet);

            // Evaluate supports over the produced plan
            for (auto&& action : ret.install_actions)
            {
                const auto& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
                const auto& vars = m_var_provider.get_or_load_dep_info_vars(action.spec, m_host_triplet);
                // Evaluate core supports condition
                const auto& supports_expr = scfl.source_control_file->core_paragraph->supports_expression;
                if (!supports_expr.evaluate(vars))
                {
                    ret.unsupported_features.emplace(std::piecewise_construct,
                                                     std::forward_as_tuple(action.spec, FeatureNameCore),
                                                     std::forward_as_tuple(supports_expr));
                }

                // Evaluate per-feature supports conditions
                for (auto&& fdeps : action.feature_dependencies)
                {
                    if (fdeps.first == FeatureNameCore) continue;

                    auto& fpgh = scfl.source_control_file->find_feature(fdeps.first).value_or_exit(VCPKG_LINE_INFO);
                    if (!fpgh.supports_expression.evaluate(vars))
                    {
                        ret.unsupported_features.emplace(std::piecewise_construct,
                                                         std::forward_as_tuple(action.spec, fdeps.first),
                                                         std::forward_as_tuple(fpgh.supports_expression));
                    }
                }
            }

            if (unsupported_port_action == UnsupportedPortAction::Error && !ret.unsupported_features.empty())
            {
                LocalizedString msg;
                for (auto&& f : ret.unsupported_features)
                {
                    if (!msg.empty()) msg.append_raw("\n");

                    const auto feature_spec =
                        f.first.feature() == FeatureNameCore
                            ? f.first.spec().name()
                            : format_name_only_feature_spec(f.first.spec().name(), f.first.feature());

                    msg.append(msgUnsupportedFeatureSupportsExpression,
                               msg::package_name = f.first.spec().name(),
                               msg::feature_spec = feature_spec,
                               msg::supports_expression = to_string(f.second),
                               msg::triplet = f.first.spec().triplet());
                }

                return msg;
            }
            return ret;
        }
    }

    ExpectedL<ActionPlan> create_versioned_install_plan(const IVersionedPortfileProvider& provider,
                                                        const IBaselineProvider& bprovider,
                                                        const IOverlayProvider& oprovider,
                                                        const CMakeVars::CMakeVarProvider& var_provider,
                                                        const std::vector<Dependency>& deps,
                                                        const std::vector<DependencyOverride>& overrides,
                                                        const PackageSpec& toplevel,
                                                        PackagesDirAssigner& packages_dir_assigner,
                                                        const CreateInstallPlanOptions& options)
    {
        VersionedPackageGraph vpg(
            provider, bprovider, oprovider, var_provider, toplevel, options.host_triplet, packages_dir_assigner);
        for (auto&& o : overrides)
        {
            vpg.add_override(o.name, o.version);
        }

        vpg.solve_with_roots(deps);
        return vpg.finalize_extract_plan(options.unsupported_port_action,
                                         options.use_head_version_if_user_requested,
                                         options.editable_if_user_requested);
    }
}
