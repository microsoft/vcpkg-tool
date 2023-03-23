#include <vcpkg/base/files.h>
#include <vcpkg/base/graphs.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/cmakevars.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/metrics.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg
{
    namespace
    {
        struct ClusterGraph;

        struct ClusterInstalled
        {
            ClusterInstalled(const InstalledPackageView& ipv) : ipv(ipv)
            {
                original_features.emplace("core");
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
                    return inst->original_features.find(feature) != inst->original_features.end();
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
                                           return inst->original_features.find(feature) !=
                                                  inst->original_features.end();
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
                if (feature == "default")
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

                Util::Vectors::append(&out_new_dependencies, dep_list);
            }

            void create_install_info(std::vector<FeatureSpec>& out_reinstall_requirements)
            {
                bool defaults_requested = false;
                if (const ClusterInstalled* inst = m_installed.get())
                {
                    out_reinstall_requirements.emplace_back(m_spec, "core");
                    auto& scfl = get_scfl_or_exit();
                    for (const std::string& installed_feature : inst->original_features)
                    {
                        if (scfl.source_control_file->find_feature(installed_feature).has_value())
                            out_reinstall_requirements.emplace_back(m_spec, installed_feature);
                    }
                    defaults_requested = inst->defaults_requested;
                }

                Checks::check_exit(VCPKG_LINE_INFO, !m_install_info.has_value());
                m_install_info = make_optional(ClusterInstallInfo{});

                if (defaults_requested)
                {
                    out_reinstall_requirements.emplace_back(m_spec, "default");
                }
                else if (request_type != RequestType::USER_REQUESTED)
                {
                    out_reinstall_requirements.emplace_back(m_spec, "default");
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
                if (spec.feature() == "core")
                {
                    return get_scfl_or_exit().source_control_file->core_paragraph->supports_expression;
                }
                else if (spec.feature() != "default")
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
                    return p_installed->ipv.core->package.get_version();
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
                         Triplet host_triplet);
            ~PackageGraph() = default;

            void install(Span<const FeatureSpec> specs, UnsupportedPortAction unsupported_port_action);
            void upgrade(Span<const PackageSpec> specs, UnsupportedPortAction unsupported_port_action);
            void mark_user_requested(const PackageSpec& spec);

            ActionPlan serialize(GraphRandomizer* randomizer) const;

            void mark_for_reinstall(const PackageSpec& spec,
                                    std::vector<FeatureSpec>& out_reinstall_requirements) const;
            const CMakeVars::CMakeVarProvider& m_var_provider;

            std::unique_ptr<ClusterGraph> m_graph;
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

    static std::string to_output_string(RequestType request_type,
                                        const ZStringView s,
                                        const BuildPackageOptions& options,
                                        const SourceControlFileAndLocation* scfl,
                                        const InstalledPackageView* ipv,
                                        const Path& builtin_ports_dir)
    {
        std::string ret;
        switch (request_type)
        {
            case RequestType::AUTO_SELECTED: Strings::append(ret, "  * "); break;
            case RequestType::USER_REQUESTED: Strings::append(ret, "    "); break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
        Strings::append(ret, s);
        if (scfl)
        {
            Strings::append(ret, " -> ", scfl->to_version());
        }
        else if (ipv)
        {
            Strings::append(ret, " -> ", Version{ipv->core->package.version, ipv->core->package.port_version});
        }
        if (options.use_head_version == UseHeadVersion::YES)
        {
            Strings::append(ret, " (+HEAD)");
        }
        if (scfl)
        {
            if (!builtin_ports_dir.empty() &&
                !Strings::case_insensitive_ascii_starts_with(scfl->source_location, builtin_ports_dir))
            {
                Strings::append(ret, " -- ", scfl->source_location);
            }
        }
        return ret;
    }

    std::string to_output_string(RequestType request_type, const ZStringView s, const BuildPackageOptions& options)
    {
        return to_output_string(request_type, s, options, {}, {}, {});
    }

    std::string to_output_string(RequestType request_type, const ZStringView s)
    {
        return to_output_string(request_type, s, {}, {}, {}, {});
    }

    InstallPlanAction::InstallPlanAction() noexcept
        : plan_type(InstallPlanType::UNKNOWN), request_type(RequestType::UNKNOWN), build_options{}
    {
    }

    InstallPlanAction::InstallPlanAction(const PackageSpec& spec,
                                         const SourceControlFileAndLocation& scfl,
                                         const RequestType& request_type,
                                         Triplet host_triplet,
                                         std::map<std::string, std::vector<FeatureSpec>>&& dependencies,
                                         std::vector<LocalizedString>&& build_failure_messages,
                                         std::vector<std::string> default_features)
        : spec(spec)
        , source_control_file_and_location(scfl)
        , default_features(std::move(default_features))
        , plan_type(InstallPlanType::BUILD_AND_INSTALL)
        , request_type(request_type)
        , build_options{}
        , feature_dependencies(std::move(dependencies))
        , build_failure_messages(std::move(build_failure_messages))
        , host_triplet(host_triplet)
    {
        for (const auto& kv : feature_dependencies)
        {
            feature_list.emplace_back(kv.first);
            for (const FeatureSpec& fspec : kv.second)
            {
                if (spec != fspec.spec())
                {
                    package_dependencies.emplace_back(fspec.spec());
                }
            }
        }

        Util::sort_unique_erase(package_dependencies);
        Util::sort_unique_erase(feature_list);
    }

    InstallPlanAction::InstallPlanAction(InstalledPackageView&& ipv, const RequestType& request_type)
        : spec(ipv.spec())
        , installed_package(std::move(ipv))
        , plan_type(InstallPlanType::ALREADY_INSTALLED)
        , request_type(request_type)
        , build_options{}
        , feature_dependencies(installed_package.get()->feature_dependencies())
        , package_dependencies(installed_package.get()->dependencies())
    {
        for (const auto& kv : feature_dependencies)
        {
            feature_list.emplace_back(kv.first);
        }
    }

    std::string InstallPlanAction::displayname() const
    {
        if (this->feature_list.empty())
        {
            return this->spec.to_string();
        }

        const std::string features = Strings::join(",", feature_list);
        return fmt::format("{}[{}]:{}", this->spec.name(), features, this->spec.triplet());
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

    bool InstallPlanAction::compare_by_name(const InstallPlanAction* left, const InstallPlanAction* right)
    {
        return left->spec.name() < right->spec.name();
    }

    RemovePlanAction::RemovePlanAction() noexcept
        : plan_type(RemovePlanType::UNKNOWN), request_type(RequestType::UNKNOWN)
    {
    }

    RemovePlanAction::RemovePlanAction(const PackageSpec& spec,
                                       const RemovePlanType& plan_type,
                                       const RequestType& request_type)
        : spec(spec), plan_type(plan_type), request_type(request_type)
    {
    }

    template<class Message>
    static LocalizedString create_unsupported_message(Message m,
                                                      const FeatureSpec& spec,
                                                      const PlatformExpression::Expr& expr)
    {
        const auto feature_spec =
            (spec.feature() == "core" ? spec.port() : format_name_only_feature_spec(spec.port(), spec.feature()));
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
                (spec.feature() == "core" ? spec.port() : format_name_only_feature_spec(spec.port(), spec.feature()));
            msg::println_warning(msgUnsupportedFeatureSupportsExpressionWarning,
                                 msg::feature_spec = feature_spec,
                                 msg::supports_expression = to_string(entry.second),
                                 msg::triplet = spec.triplet());
        }
    }

    bool ExportPlanAction::compare_by_name(const ExportPlanAction* left, const ExportPlanAction* right)
    {
        return left->spec.name() < right->spec.name();
    }

    ExportPlanAction::ExportPlanAction() noexcept
        : plan_type(ExportPlanType::UNKNOWN), request_type(RequestType::UNKNOWN)
    {
    }

    ExportPlanAction::ExportPlanAction(const PackageSpec& spec,
                                       InstalledPackageView&& installed_package,
                                       const RequestType& request_type)
        : spec(spec)
        , plan_type(ExportPlanType::ALREADY_BUILT)
        , request_type(request_type)
        , m_installed_package(std::move(installed_package))
    {
    }

    ExportPlanAction::ExportPlanAction(const PackageSpec& spec, const RequestType& request_type)
        : spec(spec), plan_type(ExportPlanType::NOT_BUILT), request_type(request_type)
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

    bool RemovePlanAction::compare_by_name(const RemovePlanAction* left, const RemovePlanAction* right)
    {
        return left->spec.name() < right->spec.name();
    }

    std::vector<RemovePlanAction> create_remove_plan(const std::vector<PackageSpec>& specs,
                                                     const StatusParagraphs& status_db)
    {
        struct RemoveAdjacencyProvider final : AdjacencyProvider<PackageSpec, RemovePlanAction>
        {
            const StatusParagraphs& status_db;
            const std::vector<InstalledPackageView>& installed_ports;
            const std::unordered_set<PackageSpec>& specs_as_set;

            RemoveAdjacencyProvider(const StatusParagraphs& status_db,
                                    const std::vector<InstalledPackageView>& installed_ports,
                                    const std::unordered_set<PackageSpec>& specs_as_set)
                : status_db(status_db), installed_ports(installed_ports), specs_as_set(specs_as_set)
            {
            }

            std::vector<PackageSpec> adjacency_list(const RemovePlanAction& plan) const override
            {
                if (plan.plan_type == RemovePlanType::NOT_INSTALLED)
                {
                    return {};
                }

                const PackageSpec& spec = plan.spec;
                std::vector<PackageSpec> dependents;
                for (auto&& ipv : installed_ports)
                {
                    auto deps = ipv.dependencies();

                    if (std::find(deps.begin(), deps.end(), spec) == deps.end()) continue;

                    dependents.push_back(ipv.spec());
                }

                return dependents;
            }

            RemovePlanAction load_vertex_data(const PackageSpec& spec) const override
            {
                const RequestType request_type = specs_as_set.find(spec) != specs_as_set.end()
                                                     ? RequestType::USER_REQUESTED
                                                     : RequestType::AUTO_SELECTED;
                const StatusParagraphs::const_iterator it = status_db.find_installed(spec);
                if (it == status_db.end())
                {
                    return RemovePlanAction{spec, RemovePlanType::NOT_INSTALLED, request_type};
                }
                return RemovePlanAction{spec, RemovePlanType::REMOVE, request_type};
            }

            std::string to_string(const PackageSpec& spec) const override { return spec.to_string(); }
        };

        auto installed_ports = get_installed_ports(status_db);
        const std::unordered_set<PackageSpec> specs_as_set(specs.cbegin(), specs.cend());
        return topological_sort(specs, RemoveAdjacencyProvider{status_db, installed_ports, specs_as_set}, {});
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
                const RequestType request_type = specs_as_set.find(spec) != specs_as_set.end()
                                                     ? RequestType::USER_REQUESTED
                                                     : RequestType::AUTO_SELECTED;

                auto maybe_ipv = status_db.get_installed_package_view(spec);

                if (auto p_ipv = maybe_ipv.get())
                {
                    return ExportPlanAction{spec, std::move(*p_ipv), request_type};
                }

                return ExportPlanAction{spec, request_type};
            }

            std::string to_string(const PackageSpec& spec) const override { return spec.to_string(); }
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
                                           const CreateInstallPlanOptions& options)
    {
        PackageGraph pgraph(port_provider, var_provider, status_db, options.host_triplet);

        std::vector<FeatureSpec> feature_specs;
        for (const FullPackageSpec& spec : specs)
        {
            pgraph.mark_user_requested(spec.package_spec);
            spec.expand_fspecs_to(feature_specs);
        }
        Util::sort_unique_erase(feature_specs);

        pgraph.install(feature_specs, options.unsupported_port_action);

        auto res = pgraph.serialize(options.randomizer);

        return res;
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

                if (spec.feature() == "*")
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
                    if (spec.feature() == "core")
                    {
                        paragraph_depends = &clust.get_scfl_or_exit().source_control_file->core_paragraph->dependencies;
                        has_supports = !clust.get_scfl_or_exit()
                                            .source_control_file->core_paragraph->supports_expression.is_empty();
                    }
                    else if (spec.feature() == "default")
                    {
                        has_supports = Util::any_of(
                            clust.get_scfl_or_exit().source_control_file->core_paragraph->default_features,
                            [](const Dependency::Feature& feature) { return !feature.platform.is_empty(); });
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
                        if (spec.feature() == "default")
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
                                   const CreateInstallPlanOptions& options)
    {
        PackageGraph pgraph(port_provider, var_provider, status_db, options.host_triplet);

        pgraph.upgrade(specs, options.unsupported_port_action);

        return pgraph.serialize(options.randomizer);
    }

    ActionPlan PackageGraph::serialize(GraphRandomizer* randomizer) const
    {
        struct BaseEdgeProvider : AdjacencyProvider<PackageSpec, const Cluster*>
        {
            BaseEdgeProvider(const ClusterGraph& parent) : m_parent(parent) { }

            std::string to_string(const PackageSpec& spec) const override { return spec.to_string(); }
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

        for (auto&& p_cluster : remove_toposort)
        {
            plan.remove_actions.emplace_back(p_cluster->m_spec, RemovePlanType::REMOVE, p_cluster->request_type);
        }

        for (auto&& p_cluster : insert_toposort)
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
                        if (fspec.feature() != "default")
                        {
                            fspecs.insert(fspec);
                            continue;
                        }
                        auto&& dep_clust = m_graph->get(fspec.spec());
                        const auto& default_features = [&] {
                            if (dep_clust.m_install_info.has_value())
                                return dep_clust.m_install_info.get()->default_features;
                            if (auto p = dep_clust.m_installed.get()) return p->ipv.core->package.default_features;
                            Checks::unreachable(VCPKG_LINE_INFO);
                        }();
                        for (auto&& default_feature : default_features)
                            fspecs.emplace(fspec.spec(), default_feature);
                    }
                    computed_edges[kv.first].assign(fspecs.begin(), fspecs.end());
                }
                plan.install_actions.emplace_back(p_cluster->m_spec,
                                                  p_cluster->get_scfl_or_exit(),
                                                  p_cluster->request_type,
                                                  m_graph->m_host_triplet,
                                                  std::move(computed_edges),
                                                  std::move(constraint_violations),
                                                  std::move(info_ptr->default_features));
            }
            else if (p_cluster->request_type == RequestType::USER_REQUESTED && p_cluster->m_installed.has_value())
            {
                auto&& installed = p_cluster->m_installed.value_or_exit(VCPKG_LINE_INFO);
                plan.already_installed.emplace_back(InstalledPackageView(installed.ipv), p_cluster->request_type);
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
                               Triplet host_triplet)
        : m_var_provider(var_provider), m_graph(create_feature_install_graph(port_provider, status_db, host_triplet))
    {
    }

    void print_plan(const ActionPlan& action_plan, const bool is_recursive, const Path& builtin_ports_dir)
    {
        if (action_plan.remove_actions.empty() && action_plan.already_installed.empty() &&
            action_plan.install_actions.empty())
        {
            msg::println(msgInstalledRequestedPackages);
            return;
        }

        std::set<PackageSpec> remove_specs;
        std::vector<const InstallPlanAction*> rebuilt_plans;
        std::vector<const InstallPlanAction*> only_install_plans;
        std::vector<const InstallPlanAction*> new_plans;
        std::vector<const InstallPlanAction*> already_installed_plans;
        std::vector<const InstallPlanAction*> excluded;

        const bool has_non_user_requested_packages =
            Util::find_if(action_plan.install_actions, [](const InstallPlanAction& action) -> bool {
                return action.request_type != RequestType::USER_REQUESTED;
            }) != action_plan.install_actions.cend();

        for (auto&& remove_action : action_plan.remove_actions)
        {
            remove_specs.emplace(remove_action.spec);
        }
        for (auto&& install_action : action_plan.install_actions)
        {
            // remove plans are guaranteed to come before install plans, so we know the plan will be contained
            // if at all.
            auto it = remove_specs.find(install_action.spec);
            if (it != remove_specs.end())
            {
                remove_specs.erase(it);
                rebuilt_plans.push_back(&install_action);
            }
            else
            {
                if (install_action.plan_type == InstallPlanType::EXCLUDED)
                    excluded.push_back(&install_action);
                else
                    new_plans.push_back(&install_action);
            }
        }
        for (auto&& action : action_plan.already_installed)
        {
            if (action.request_type == RequestType::USER_REQUESTED) already_installed_plans.emplace_back(&action);
        }
        already_installed_plans = Util::fmap(action_plan.already_installed, [](auto&& action) { return &action; });

        std::sort(rebuilt_plans.begin(), rebuilt_plans.end(), &InstallPlanAction::compare_by_name);
        std::sort(only_install_plans.begin(), only_install_plans.end(), &InstallPlanAction::compare_by_name);
        std::sort(new_plans.begin(), new_plans.end(), &InstallPlanAction::compare_by_name);
        std::sort(already_installed_plans.begin(), already_installed_plans.end(), &InstallPlanAction::compare_by_name);
        std::sort(excluded.begin(), excluded.end(), &InstallPlanAction::compare_by_name);

        static auto actions_to_output_string = [&](const std::vector<const InstallPlanAction*>& v) {
            return Strings::join("\n", v, [&](const InstallPlanAction* p) {
                return to_output_string(p->request_type,
                                        p->displayname(),
                                        p->build_options,
                                        p->source_control_file_and_location.get(),
                                        p->installed_package.get(),
                                        builtin_ports_dir);
            });
        };

        if (!excluded.empty())
        {
            msg::println(
                msg::format(msgExcludedPackages).append_raw('\n').append_raw(actions_to_output_string(excluded)));
        }

        if (!already_installed_plans.empty())
        {
            msg::println(msg::format(msgInstalledPackages)
                             .append_raw('\n')
                             .append_raw(actions_to_output_string(already_installed_plans)));
        }

        if (!remove_specs.empty())
        {
            auto message = msg::format(msgPackagesToRemove);
            for (auto&& spec : remove_specs)
            {
                message.append_raw('\n').append_raw(to_output_string(RequestType::USER_REQUESTED, spec.to_string()));
            }
            msg::println(message);
        }

        if (!rebuilt_plans.empty())
        {
            msg::println(
                msg::format(msgPackagesToRebuild).append_raw('\n').append_raw(actions_to_output_string(rebuilt_plans)));
        }

        if (!new_plans.empty())
        {
            msg::println(
                msg::format(msgPackagesToInstall).append_raw('\n').append_raw(actions_to_output_string(new_plans)));
        }

        if (!only_install_plans.empty())
        {
            msg::println(msg::format(msgPackagesToInstallDirectly)
                             .append_raw('\n')
                             .append_raw(actions_to_output_string(only_install_plans)));
        }

        if (has_non_user_requested_packages)
        {
            msg::println(msgPackagesToModify);
        }

        bool have_removals = !remove_specs.empty() || !rebuilt_plans.empty();
        if (have_removals && !is_recursive)
        {
            msg::println_warning(msgPackagesToRebuildSuggestRecurse);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }

    namespace
    {
        struct VersionedPackageGraph
        {
            VersionedPackageGraph(const IVersionedPortfileProvider& ver_provider,
                                  const IBaselineProvider& base_provider,
                                  const IOverlayProvider& oprovider,
                                  const CMakeVars::CMakeVarProvider& var_provider,
                                  Triplet host_triplet)
                : m_ver_provider(ver_provider)
                , m_base_provider(base_provider)
                , m_o_provider(oprovider)
                , m_var_provider(var_provider)
                , m_host_triplet(host_triplet)
            {
            }

            void add_override(const std::string& name, const Version& v);

            void add_roots(View<Dependency> dep, const PackageSpec& toplevel);

            ExpectedL<ActionPlan> finalize_extract_plan(const PackageSpec& toplevel,
                                                        UnsupportedPortAction unsupported_port_action);

        private:
            const IVersionedPortfileProvider& m_ver_provider;
            const IBaselineProvider& m_base_provider;
            const IOverlayProvider& m_o_provider;
            const CMakeVars::CMakeVarProvider& m_var_provider;
            const Triplet m_host_triplet;

            struct DepSpec
            {
                PackageSpec spec;
                Version ver;
                std::vector<Dependency::Feature> features;
            };

            // This object contains the current version within a given version scheme (except for the "string" scheme,
            // there we save an object for every version)
            struct VersionSchemeInfo
            {
                VersionScheme scheme;
                const SourceControlFileAndLocation* scfl = nullptr;
                Version version;
                // This tracks a list of constraint sources for debugging purposes
                std::vector<std::string> origins;
                // mapping from feature name -> dependencies of this feature
                std::map<std::string, std::vector<FeatureSpec>> deps;

                bool is_less_than(const Version& new_ver) const;
            };

            struct PackageNode
            {
                // Mapping from version to the newest version in the corresponding version scheme
                // For example, given the versions:
                //   - "version-string": "1.0.0"
                //   - "version": "1.0.1"
                //   - "version": "1.0.2"
                // you'd have a map:
                //   {
                //     "1.0.0": { "version-string": "1.0.0" },
                //     "1.0.1": { "version": "1.0.2" },
                //     "1.0.2": { "version": "1.0.2" }
                //  }
                std::map<Version, VersionSchemeInfo*, VersionMapLess> vermap;
                // We don't know how to compare "version-string" versions, so keep all the versions separately
                std::map<std::string, VersionSchemeInfo> exacts;
                // for each version type besides string (relaxed-semver, date), we only track the latest version
                // required
                Optional<std::unique_ptr<VersionSchemeInfo>> relaxed_semver;
                Optional<std::unique_ptr<VersionSchemeInfo>> date;
                std::set<std::string> requested_features;
                bool default_features = true;
                bool user_requested = false;

                VersionSchemeInfo* get_node(const Version& ver);
                // Adds the version to the version resolver:
                //   - for string version schemes, just adds the newer version to the set
                //   - for non-string version schemes:
                //     - if the scheme doesn't exist in the set, adds the version to the set
                //     - if the scheme already exists in the set, and the version is newer than the existing entry,
                //     replaces the current entry for the scheme
                VersionSchemeInfo& emplace_node(VersionScheme scheme, const Version& ver);

                PackageNode() = default;
                PackageNode(const PackageNode&) = delete;
                PackageNode(PackageNode&&) = default;
                PackageNode& operator=(const PackageNode&) = delete;
                PackageNode& operator=(PackageNode&&) = default;

                template<class F>
                void foreach_vsi(F f)
                {
                    if (auto r = this->relaxed_semver.get())
                    {
                        f(**r);
                    }
                    if (auto d = this->date.get())
                    {
                        f(**d);
                    }
                    for (auto&& vsi : this->exacts)
                    {
                        f(vsi.second);
                    }
                }
            };

            // the roots of the dependency graph (given in the manifest file)
            std::vector<DepSpec> m_roots;
            // mapping from portname -> version. "overrides" field in manifest file
            std::map<std::string, Version> m_overrides;
            // mapping from { package specifier -> node containing resolution information for that package }
            std::map<PackageSpec, PackageNode> m_graph;

            std::pair<const PackageSpec, PackageNode>& emplace_package(const PackageSpec& spec);

            // the following functions will add stuff recursively
            void require_dependency(std::pair<const PackageSpec, PackageNode>& ref,
                                    const Dependency& dep,
                                    const std::string& origin);
            void require_port_version(std::pair<const PackageSpec, PackageNode>& graph_entry,
                                      const Version& ver,
                                      const std::string& origin);
            void require_port_feature(std::pair<const PackageSpec, PackageNode>& ref,
                                      const std::string& feature,
                                      const std::string& origin);

            void require_port_defaults(std::pair<const PackageSpec, PackageNode>& ref, const std::string& origin);

            void add_feature_to(std::pair<const PackageSpec, PackageNode>& ref,
                                VersionSchemeInfo& vsi,
                                const std::string& feature);

            ExpectedL<Version> dep_to_version(const std::string& name, const DependencyConstraint& dc);
            bool evaluate(const PackageSpec& spec, const PlatformExpression::Expr& platform_expr);

            static LocalizedString format_incomparable_versions_message(const PackageSpec& on,
                                                                        StringView from,
                                                                        const VersionSchemeInfo& current,
                                                                        const VersionSchemeInfo& target);
            std::vector<LocalizedString> m_errors;
        };

        VersionedPackageGraph::VersionSchemeInfo& VersionedPackageGraph::PackageNode::emplace_node(VersionScheme scheme,
                                                                                                   const Version& ver)
        {
            auto it = vermap.find(ver);
            if (it != vermap.end()) return *it->second;

            VersionSchemeInfo* vsi = nullptr;
            if (scheme == VersionScheme::String)
            {
                vsi = &exacts[ver.text()];
            }
            else if (scheme == VersionScheme::Relaxed || scheme == VersionScheme::Semver)
            {
                if (auto p = relaxed_semver.get())
                {
                    vsi = p->get();
                }
                else
                {
                    relaxed_semver = std::make_unique<VersionSchemeInfo>();
                    vsi = relaxed_semver.get()->get();
                }
            }
            else if (scheme == VersionScheme::Date)
            {
                if (auto p = date.get())
                {
                    vsi = p->get();
                }
                else
                {
                    date = std::make_unique<VersionSchemeInfo>();
                    vsi = date.get()->get();
                }
            }
            else
            {
                // not implemented
                Checks::unreachable(VCPKG_LINE_INFO);
            }
            vsi->scheme = scheme;
            vermap.emplace(ver, vsi);
            return *vsi;
        }

        VersionedPackageGraph::VersionSchemeInfo* VersionedPackageGraph::PackageNode::get_node(const Version& ver)
        {
            auto it = vermap.find(ver);
            return it == vermap.end() ? nullptr : it->second;
        }

        bool VersionedPackageGraph::VersionSchemeInfo::is_less_than(const Version& new_ver) const
        {
            Checks::check_exit(VCPKG_LINE_INFO, scfl);
            ASSUME(scfl != nullptr);
            auto s = scfl->source_control_file->core_paragraph->version_scheme;
            auto r = compare_versions(s, version, s, new_ver);
            Checks::check_exit(VCPKG_LINE_INFO, r != VerComp::unk);
            return r == VerComp::lt;
        }

        void VersionedPackageGraph::add_feature_to(std::pair<const PackageSpec, PackageNode>& ref,
                                                   VersionSchemeInfo& vsi,
                                                   const std::string& feature)
        {
            auto deps = vsi.scfl->source_control_file->find_dependencies_for_feature(feature);
            if (!deps)
            {
                // This version doesn't have this feature. This may result in an error during finalize if the
                // constraint is not removed via upgrades.
                return;
            }
            auto p = vsi.deps.emplace(feature, std::vector<FeatureSpec>{});
            if (!p.second)
            {
                // This feature has already been handled
                return;
            }

            for (auto&& dep : *deps.get())
            {
                PackageSpec dep_spec(dep.name, dep.host ? m_host_triplet : ref.first.triplet());

                if (!dep.platform.is_empty())
                {
                    auto maybe_vars = m_var_provider.get_dep_info_vars(ref.first);
                    if (!maybe_vars)
                    {
                        m_var_provider.load_dep_info_vars({&ref.first, 1}, m_host_triplet);
                        maybe_vars = m_var_provider.get_dep_info_vars(ref.first);
                    }

                    if (!dep.platform.evaluate(maybe_vars.value_or_exit(VCPKG_LINE_INFO)))
                    {
                        continue;
                    }
                }

                auto& dep_node = emplace_package(dep_spec);
                if (dep_spec == ref.first)
                {
                    // this is a feature dependency for oneself
                    for (auto&& f : dep.features)
                    {
                        if (evaluate(ref.first, f.platform))
                        {
                            require_port_feature(ref, f.name, ref.first.name());
                        }
                    }
                }
                else
                {
                    require_dependency(dep_node, dep, ref.first.name());
                }

                p.first->second.emplace_back(dep_spec, "core");
                for (auto&& f : dep.features)
                {
                    if (evaluate(ref.first, f.platform))
                    {
                        p.first->second.emplace_back(dep_spec, f.name);
                    }
                }
            }
        }

        void VersionedPackageGraph::require_dependency(std::pair<const PackageSpec, PackageNode>& ref,
                                                       const Dependency& dep,
                                                       const std::string& origin)
        {
            const auto maybe_overlay = m_o_provider.get_control_file(ref.first.name());
            if (auto p_overlay = maybe_overlay.get())
            {
                const auto overlay_version = p_overlay->source_control_file->to_version();
                require_port_version(ref, overlay_version, origin);
            }
            else if (const auto over_it = m_overrides.find(ref.first.name()); over_it != m_overrides.end())
            {
                require_port_version(ref, over_it->second, origin);
            }
            else
            {
                const auto base_ver = m_base_provider.get_baseline_version(dep.name);
                const auto dep_ver = dep.constraint.try_get_minimum_version();

                if (auto dv = dep_ver.get())
                {
                    require_port_version(ref, *dv, origin);
                }

                if (auto bv = base_ver.get())
                {
                    require_port_version(ref, *bv, origin);
                }
            }

            for (auto&& f : dep.features)
            {
                if (evaluate(ref.first, f.platform))
                {
                    require_port_feature(ref, f.name, origin);
                }
            }

            if (dep.default_features)
            {
                require_port_defaults(ref, origin);
            }
        }
        void VersionedPackageGraph::require_port_version(std::pair<const PackageSpec, PackageNode>& graph_entry,
                                                         const Version& version,
                                                         const std::string& origin)
        {
            // if this port is an overlay port, ignore the given version and use the version from the overlay
            auto maybe_overlay = m_o_provider.get_control_file(graph_entry.first.name());
            const vcpkg::SourceControlFileAndLocation* p_scfl = maybe_overlay.get();
            if (p_scfl)
            {
                const auto overlay_version = p_scfl->source_control_file->to_version();
                // If the original request did not match the overlay version, restart this function to operate on the
                // overlay version
                if (version != overlay_version)
                {
                    require_port_version(graph_entry, overlay_version, origin);
                    return;
                }
            }
            else
            {
                // if there is a override, ignore the given version and use the version from the override
                auto over_it = m_overrides.find(graph_entry.first.name());
                if (over_it != m_overrides.end() && over_it->second != version)
                {
                    require_port_version(graph_entry, over_it->second, origin);
                    return;
                }

                auto maybe_scfl = m_ver_provider.get_control_file({graph_entry.first.name(), version});
                p_scfl = maybe_scfl.get();
                if (!p_scfl)
                {
                    m_errors.push_back(std::move(maybe_scfl).error());
                    return;
                }
            }

            auto& versioned_graph_entry =
                graph_entry.second.emplace_node(p_scfl->source_control_file->core_paragraph->version_scheme, version);
            versioned_graph_entry.origins.push_back(origin);
            // Use the new source control file if we currently don't have one or the new one is newer
            bool replace;
            if (versioned_graph_entry.scfl == nullptr)
            {
                replace = true;
            }
            else if (versioned_graph_entry.scfl == p_scfl)
            {
                replace = false;
            }
            else
            {
                replace = versioned_graph_entry.is_less_than(version);
            }

            if (replace)
            {
                versioned_graph_entry.scfl = p_scfl;
                versioned_graph_entry.version = p_scfl->source_control_file->to_version();
                versioned_graph_entry.deps.clear();

                // add all dependencies to the graph
                add_feature_to(graph_entry, versioned_graph_entry, "core");

                for (auto&& f : graph_entry.second.requested_features)
                {
                    add_feature_to(graph_entry, versioned_graph_entry, f);
                }

                if (graph_entry.second.default_features)
                {
                    for (const auto& f : p_scfl->source_control_file->core_paragraph->default_features)
                    {
                        if (evaluate(graph_entry.first, f.platform))
                        {
                            add_feature_to(graph_entry, versioned_graph_entry, f.name);
                        }
                    }
                }
            }
        }

        void VersionedPackageGraph::require_port_defaults(std::pair<const PackageSpec, PackageNode>& ref,
                                                          const std::string& origin)
        {
            (void)origin;
            if (!ref.second.default_features)
            {
                ref.second.default_features = true;
                ref.second.foreach_vsi([this, &ref](VersionSchemeInfo& vsi) {
                    if (vsi.scfl)
                    {
                        for (auto&& f : vsi.scfl->source_control_file->core_paragraph->default_features)
                        {
                            if (evaluate(ref.first, f.platform))
                            {
                                this->add_feature_to(ref, vsi, f.name);
                            }
                        }
                    }
                });
            }
        }
        void VersionedPackageGraph::require_port_feature(std::pair<const PackageSpec, PackageNode>& ref,
                                                         const std::string& feature,
                                                         const std::string& origin)
        {
            if (feature == "default")
            {
                return require_port_defaults(ref, origin);
            }
            auto inserted = ref.second.requested_features.emplace(feature).second;
            if (inserted)
            {
                ref.second.foreach_vsi(
                    [this, &ref, &feature](VersionSchemeInfo& vsi) { this->add_feature_to(ref, vsi, feature); });
            }
            (void)origin;
        }

        std::pair<const PackageSpec, VersionedPackageGraph::PackageNode>& VersionedPackageGraph::emplace_package(
            const PackageSpec& spec)
        {
            return *m_graph.emplace(spec, PackageNode{}).first;
        }

        ExpectedL<Version> VersionedPackageGraph::dep_to_version(const std::string& name,
                                                                 const DependencyConstraint& dc)
        {
            auto maybe_overlay = m_o_provider.get_control_file(name);
            if (auto p_overlay = maybe_overlay.get())
            {
                return p_overlay->source_control_file->to_version();
            }

            auto over_it = m_overrides.find(name);
            if (over_it != m_overrides.end())
            {
                return over_it->second;
            }

            auto maybe_cons = dc.try_get_minimum_version();
            if (auto p = maybe_cons.get())
            {
                return std::move(*p);
            }

            return m_base_provider.get_baseline_version(name);
        }

        bool VersionedPackageGraph::evaluate(const PackageSpec& spec, const PlatformExpression::Expr& platform_expr)
        {
            return platform_expr.evaluate(m_var_provider.get_or_load_dep_info_vars(spec, m_host_triplet));
        }

        void VersionedPackageGraph::add_override(const std::string& name, const Version& v)
        {
            m_overrides.emplace(name, v);
        }

        void VersionedPackageGraph::add_roots(View<Dependency> deps, const PackageSpec& toplevel)
        {
            auto dep_to_spec = [&toplevel, this](const Dependency& d) {
                return PackageSpec{d.name, d.host ? m_host_triplet : toplevel.triplet()};
            };
            auto specs = Util::fmap(deps, dep_to_spec);

            specs.push_back(toplevel);
            Util::sort_unique_erase(specs);
            m_var_provider.load_dep_info_vars(specs, m_host_triplet);
            const auto& vars = m_var_provider.get_dep_info_vars(toplevel).value_or_exit(VCPKG_LINE_INFO);
            std::vector<const Dependency*> active_deps;

            // First add all top level packages to ensure the default_features is set to false before recursing into the
            // individual packages. Otherwise, a case like:
            //     A -> B, C[core]
            //     B -> C
            // could install the default features of C. (A is the manifest/vcpkg.json)
            for (auto&& dep : deps)
            {
                if (!dep.platform.evaluate(vars)) continue;

                active_deps.push_back(&dep);

                // Disable default features for deps with [core] as a dependency
                // Note: x[core], x[y] will still eventually depend on defaults due to the second x[y]
                if (!dep.default_features)
                {
                    auto& node = emplace_package(dep_to_spec(dep));
                    node.second.default_features = false;
                }
            }

            for (auto pdep : active_deps)
            {
                const auto& dep = *pdep;
                auto spec = dep_to_spec(dep);

                auto& node = emplace_package(spec);
                node.second.user_requested = true;

                auto maybe_overlay = m_o_provider.get_control_file(dep.name);
                auto over_it = m_overrides.find(dep.name);
                if (auto p_overlay = maybe_overlay.get())
                {
                    const auto ver = p_overlay->source_control_file->to_version();
                    m_roots.push_back(DepSpec{spec, ver, dep.features});
                    require_port_version(node, ver, toplevel.name());
                }
                else if (over_it != m_overrides.end())
                {
                    m_roots.push_back(DepSpec{spec, over_it->second, dep.features});
                    require_port_version(node, over_it->second, toplevel.name());
                }
                else
                {
                    const auto dep_ver = dep.constraint.try_get_minimum_version();
                    const auto base_ver = m_base_provider.get_baseline_version(dep.name);
                    if (auto p_dep_ver = dep_ver.get())
                    {
                        m_roots.push_back(DepSpec{spec, *p_dep_ver, dep.features});
                        if (auto p_base_ver = base_ver.get())
                        {
                            // Compare version constraint with baseline to only evaluate the "tighter" constraint
                            auto dep_scfl = m_ver_provider.get_control_file({dep.name, *p_dep_ver});
                            auto base_scfl = m_ver_provider.get_control_file({dep.name, *p_base_ver});
                            if (dep_scfl && base_scfl)
                            {
                                auto r = compare_versions(
                                    dep_scfl.get()->source_control_file->core_paragraph->version_scheme,
                                    *p_dep_ver,
                                    base_scfl.get()->source_control_file->core_paragraph->version_scheme,
                                    *p_base_ver);
                                if (r == VerComp::lt)
                                {
                                    require_port_version(node, *p_base_ver, "baseline");
                                    require_port_version(node, *p_dep_ver, toplevel.name());
                                }
                                else
                                {
                                    require_port_version(node, *p_dep_ver, toplevel.name());
                                    require_port_version(node, *p_base_ver, "baseline");
                                }
                            }
                            else
                            {
                                if (!dep_scfl) m_errors.push_back(dep_scfl.error());
                                if (!base_scfl) m_errors.push_back(base_scfl.error());
                            }
                        }
                        else
                        {
                            require_port_version(node, *p_dep_ver, toplevel.name());
                        }
                    }
                    else if (auto p_base_ver = base_ver.get())
                    {
                        m_roots.push_back(DepSpec{spec, *p_base_ver, dep.features});
                        require_port_version(node, *p_base_ver, toplevel.name());
                    }
                    else
                    {
                        m_errors.push_back(msg::format(
                            msgVersionConstraintUnresolvable, msg::package_name = dep.name, msg::spec = toplevel));
                    }
                }

                for (auto&& f : dep.features)
                {
                    if (evaluate(toplevel, f.platform))
                    {
                        require_port_feature(node, f.name, toplevel.name());
                    }
                }
            }
        }

        LocalizedString VersionedPackageGraph::format_incomparable_versions_message(const PackageSpec& on,
                                                                                    StringView from,
                                                                                    const VersionSchemeInfo& current,
                                                                                    const VersionSchemeInfo& target)
        {
            return msg::format_error(msgVersionIncomparable1,
                                     msg::spec = on,
                                     msg::package_name = from,
                                     msg::expected = target.version,
                                     msg::actual = current.version)
                .append_raw('\n')
                .append_indent()
                .append(msgVersionIncomparable2, msg::version = current.version, msg::new_scheme = current.scheme)
                .append_raw('\n')
                .append_indent()
                .append(msgVersionIncomparable2, msg::version = target.version, msg::new_scheme = target.scheme)
                .append_raw('\n')
                .append(msgVersionIncomparable3)
                .append_raw('\n')
                .append_indent()
                .append_raw("\"overrides\": [\n")
                .append_indent(2)
                .append_raw(fmt::format(R"({{ "name": "{}", "version": "{}" }})", on.name(), current.version))
                .append_raw('\n')
                .append_indent()
                .append_raw("]\n")
                .append(msgVersionIncomparable4);
        }

        // This function is called after all versioning constraints have been resolved. It is responsible for
        // serializing out the final execution graph and performing all final validations (such as all required
        // features being selected and present)
        ExpectedL<ActionPlan> VersionedPackageGraph::finalize_extract_plan(
            const PackageSpec& toplevel, UnsupportedPortAction unsupported_port_action)
        {
            if (!m_errors.empty())
            {
                Util::sort_unique_erase(m_errors);
                return LocalizedString::from_raw(Strings::join("\n", m_errors));
            }

            ActionPlan ret;

            // second == nullptr means "in progress"
            std::map<PackageSpec, VersionSchemeInfo*> emitted;
            struct Frame
            {
                InstallPlanAction ipa;
                std::vector<DepSpec> deps;
            };
            std::vector<Frame> stack;

            // Adds a new Frame to the stack if the spec was not already added
            auto push = [&emitted, this, &stack, unsupported_port_action, &ret](
                            const PackageSpec& spec,
                            const Version& new_ver,
                            const PackageSpec& origin,
                            View<std::string> features) -> Optional<LocalizedString> {
                auto&& node = emplace_package(spec).second;
                auto overlay = m_o_provider.get_control_file(spec.name());
                auto over_it = m_overrides.find(spec.name());

                VersionedPackageGraph::VersionSchemeInfo* p_vnode;
                if (auto p_overlay = overlay.get())
                    p_vnode = node.get_node(p_overlay->source_control_file->to_version());
                else if (over_it != m_overrides.end())
                    p_vnode = node.get_node(over_it->second);
                else
                    p_vnode = node.get_node(new_ver);

                if (!p_vnode)
                {
                    return msg::format_error(
                        msgVersionNotFoundDuringDiscovery, msg::spec = spec, msg::version = new_ver);
                }

                { // use if(init;condition) if we support c++17
                    const auto& supports_expr = p_vnode->scfl->source_control_file->core_paragraph->supports_expression;
                    if (!supports_expr.is_empty())
                    {
                        if (!supports_expr.evaluate(m_var_provider.get_or_load_dep_info_vars(spec, m_host_triplet)))
                        {
                            FeatureSpec feature_spec(spec, "core");

                            if (unsupported_port_action == UnsupportedPortAction::Error)
                            {
                                return msg::format_error(create_unsupported_message(
                                    msgUnsupportedFeatureSupportsExpression, feature_spec, supports_expr));
                            }

                            ret.unsupported_features.insert({FeatureSpec(spec, "core"), supports_expr});
                        }
                    }
                }

                for (auto&& f : features)
                {
                    if (f == "core") continue;
                    if (f == "default") continue;
                    auto feature = p_vnode->scfl->source_control_file->find_feature(f);
                    if (!feature)
                    {
                        return msg::format_error(msgVersionMissingRequiredFeature,
                                                 msg::spec = spec,
                                                 msg::version = new_ver,
                                                 msg::feature = f);
                    }

                    const auto& supports_expr = feature.get()->supports_expression;
                    if (!supports_expr.is_empty())
                    {
                        if (!supports_expr.evaluate(m_var_provider.get_or_load_dep_info_vars(spec, m_host_triplet)))
                        {
                            if (unsupported_port_action == UnsupportedPortAction::Error)
                            {
                                const auto feature_spec_text = format_name_only_feature_spec(spec.name(), f);
                                const auto supports_expression_text = to_string(supports_expr);
                                return msg::format_error(msgUnsupportedFeatureSupportsExpression,
                                                         msg::package_name = spec.name(),
                                                         msg::feature_spec = feature_spec_text,
                                                         msg::supports_expression = supports_expression_text,
                                                         msg::triplet = spec.triplet());
                            }

                            ret.unsupported_features.emplace(FeatureSpec{spec, f}, supports_expr);
                        }
                    }
                }

                auto p = emitted.emplace(spec, nullptr);
                if (p.second)
                {
                    // Newly inserted
                    if (!overlay && over_it == m_overrides.end())
                    {
                        // Not overridden -- Compare against baseline
                        if (auto baseline = m_base_provider.get_baseline_version(spec.name()))
                        {
                            if (auto base_node = node.get_node(*baseline.get()))
                            {
                                if (base_node != p_vnode)
                                {
                                    return format_incomparable_versions_message(spec, "baseline", *p_vnode, *base_node);
                                }
                            }
                        }
                    }

                    // -> Add stack frame
                    auto maybe_vars = m_var_provider.get_dep_info_vars(spec);

                    std::vector<std::string> default_features;
                    for (const auto& feature : p_vnode->scfl->source_control_file->core_paragraph->default_features)
                    {
                        if (feature.platform.evaluate(maybe_vars.value_or_exit(VCPKG_LINE_INFO)))
                            default_features.push_back(feature.name);
                    }
                    InstallPlanAction ipa(spec,
                                          *p_vnode->scfl,
                                          node.user_requested ? RequestType::USER_REQUESTED
                                                              : RequestType::AUTO_SELECTED,
                                          m_host_triplet,
                                          std::move(p_vnode->deps),
                                          {},
                                          std::move(default_features));
                    std::vector<DepSpec> deps;
                    for (auto&& f : ipa.feature_list)
                    {
                        if (auto maybe_deps =
                                p_vnode->scfl->source_control_file->find_dependencies_for_feature(f).get())
                        {
                            for (auto&& dep : *maybe_deps)
                            {
                                PackageSpec dep_spec(dep.name, dep.host ? m_host_triplet : spec.triplet());
                                if (dep_spec == spec) continue;

                                if (!dep.platform.is_empty() &&
                                    !dep.platform.evaluate(maybe_vars.value_or_exit(VCPKG_LINE_INFO)))
                                {
                                    continue;
                                }
                                auto maybe_cons = dep_to_version(dep.name, dep.constraint);

                                if (auto cons = maybe_cons.get())
                                {
                                    deps.emplace_back(DepSpec{std::move(dep_spec), std::move(*cons), dep.features});
                                }
                                else
                                {
                                    return msg::format_error(msgVersionConstraintUnresolvable,
                                                             msg::package_name = dep.name,
                                                             msg::spec = spec);
                                }
                            }
                        }
                    }
                    stack.push_back(Frame{std::move(ipa), std::move(deps)});
                    return nullopt;
                }
                else
                {
                    // spec already present in map
                    if (p.first->second == nullptr)
                    {
                        return msg::format_error(msgCycleDetectedDuring, msg::spec = spec)
                            .append_raw('\n')
                            .append_raw(Strings::join(
                                "\n", stack, [](const auto& p) -> const PackageSpec& { return p.ipa.spec; }));
                    }
                    else if (p.first->second != p_vnode)
                    {
                        // comparable versions should retrieve the same info node
                        return format_incomparable_versions_message(
                            spec, origin.to_string(), *p_vnode, *p.first->second);
                    }
                    return nullopt;
                }
            };

            for (auto&& root : m_roots)
            {
                if (auto err = push(
                        root.spec, root.ver, toplevel, Util::fmap(root.features, [](const auto& f) { return f.name; })))
                {
                    return std::move(*err.get());
                }

                while (stack.size() > 0)
                {
                    auto& back = stack.back();
                    if (back.deps.empty())
                    {
                        emitted[back.ipa.spec] =
                            emplace_package(back.ipa.spec)
                                .second.get_node(
                                    back.ipa.source_control_file_and_location.get()->source_control_file->to_version());
                        ret.install_actions.push_back(std::move(back.ipa));
                        stack.pop_back();
                    }
                    else
                    {
                        auto dep = std::move(back.deps.back());
                        back.deps.pop_back();
                        if (auto err =
                                push(dep.spec, dep.ver, back.ipa.spec, Util::fmap(dep.features, [](const auto& f) {
                                         return f.name;
                                     })))
                        {
                            return std::move(*err.get());
                        }
                    }
                }
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
                                                        Triplet host_triplet,
                                                        UnsupportedPortAction unsupported_port_action)
    {
        VersionedPackageGraph vpg(provider, bprovider, oprovider, var_provider, host_triplet);
        for (auto&& o : overrides)
        {
            vpg.add_override(o.name, {o.version, o.port_version});
        }

        vpg.add_roots(deps, toplevel);
        return vpg.finalize_extract_plan(toplevel, unsupported_port_action);
    }
}
