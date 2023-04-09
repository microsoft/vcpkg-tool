#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/cmakevars.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/triplet.h>

namespace
{
    using namespace vcpkg;

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
     * CMake for calculationg dependency resolution tags. However, the results are sensitive to the definition of
     * comparison. If "compares >= the baseline" changes, the set of considered constraints will change, and so will the
     * results.
     */

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

        void solve_with_roots(View<Dependency> dep, const PackageSpec& toplevel);

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
            DependencyConstraint dc;
            std::vector<std::string> features;
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

        LocalizedString format_incomparable_versions_message(const PackageSpec& on,
                                                             StringView from,
                                                             const SchemedVersion& baseline,
                                                             const SchemedVersion& target) const;
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
                    spec_set.insert({d.name, d.host ? m_host_triplet : s.spec.triplet()});
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
                    if (f == "default") abort();
                    require_port_feature(*node, f, frame.spec.name());
                }
                if (Util::find(dep.features, StringView{"core"}) == dep.features.end())
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
                    auto deps = scfl->source_control_file->find_dependencies_for_feature(f);
                    if (!deps) continue;
                    m_resolve_stack.push_back({ref.first, *deps.get()});
                }
            }
        }
    }
    void VersionedPackageGraph::require_port_feature(PackageNode& ref,
                                                     const std::string& feature,
                                                     const std::string& origin)
    {
        if (feature == "default")
        {
            return require_port_defaults(ref, origin);
        }
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

        if (ref.second.considered.find(scfl) != ref.second.considered.end()) return;
        ref.second.considered.insert(scfl);

        auto features = ref.second.requested_features;
        if (ref.second.default_features)
        {
            const auto& defaults = ref.second.scfl->source_control_file->core_paragraph->default_features;
            features.insert(defaults.begin(), defaults.end());
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
        auto it = m_graph.find(spec);
        if (it != m_graph.end())
        {
            it->second.origins.insert(origin);
            return *it;
        }

        if (m_failed_nodes.find(spec.name()) != m_failed_nodes.end())
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

        // Implicit defaults are disabled if spec has been mentioned at top-level.
        // Note that if top-level doesn't also mark that reference as `[core]`, defaults will be re-engaged.
        it->second.default_features = m_user_requested.find(spec) == m_user_requested.end();
        it->second.requested_features.insert("core");

        require_scfl(*it, it->second.scfl, origin);
        return *it;
    }

    void VersionedPackageGraph::add_override(const std::string& name, const Version& v)
    {
        m_overrides.emplace(name, v);
    }

    void VersionedPackageGraph::solve_with_roots(View<Dependency> deps, const PackageSpec& toplevel)
    {
        auto dep_to_spec = [&toplevel, this](const Dependency& d) {
            return PackageSpec{d.name, d.host ? m_host_triplet : toplevel.triplet()};
        };
        auto specs = Util::fmap(deps, dep_to_spec);

        specs.push_back(toplevel);
        Util::sort_unique_erase(specs);
        for (auto&& dep : deps)
        {
            if (!dep.platform.is_empty() &&
                !dep.platform.evaluate(m_var_provider.get_or_load_dep_info_vars(toplevel, m_host_triplet)))
            {
                continue;
            }

            auto spec = dep_to_spec(dep);
            m_user_requested.insert(spec);
            m_roots.push_back(DepSpec{std::move(spec), dep.constraint, dep.features});
        }

        m_resolve_stack.push_back({toplevel, deps});

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
                                                                                const SchemedVersion& target) const
    {
        return msg::format_error(msgVersionIncomparable1,
                                 msg::spec = on,
                                 msg::constraint_origin = from,
                                 msg::expected = target.version,
                                 msg::actual = baseline.version)
            .append_raw('\n')
            .append_indent()
            .append(msgVersionIncomparable2, msg::version = baseline.version, msg::new_scheme = baseline.scheme)
            .append_raw('\n')
            .append_indent()
            .append(msgVersionIncomparable2, msg::version = target.version, msg::new_scheme = target.scheme)
            .append_raw('\n')
            .append(msgVersionIncomparable3)
            .append_raw('\n')
            .append_indent()
            .append_raw("\"overrides\": [\n")
            .append_indent(2)
            .append_raw(fmt::format(R"({{ "name": "{}", "version": "{}" }})", on.name(), baseline.version))
            .append_raw('\n')
            .append_indent()
            .append_raw("]\n")
            .append(msgVersionIncomparable4);
    }

    std::map<std::string, std::vector<FeatureSpec>> VersionedPackageGraph::compute_feature_dependencies(
        const PackageNode& node, std::vector<DepSpec>& out_dep_specs) const
    {
        std::map<std::string, std::vector<FeatureSpec>> feature_deps;
        std::set<std::string> all_features = node.second.requested_features;
        if (node.second.default_features)
        {
            const auto& f = node.second.scfl->source_control_file->core_paragraph->default_features;
            all_features.insert(f.begin(), f.end());
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
                        !fdep.platform.evaluate(m_var_provider.get_or_load_dep_info_vars(node.first, m_host_triplet)))
                    {
                        continue;
                    }

                    fspecs.emplace_back(fspec, "core");
                    for (auto&& g : fdep.features)
                        fspecs.emplace_back(fspec, g);
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
    ExpectedL<ActionPlan> VersionedPackageGraph::finalize_extract_plan(const PackageSpec& toplevel,
                                                                       UnsupportedPortAction unsupported_port_action)
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
        auto push = [&emitted, this, &stack](const DepSpec& dep, StringView origin) -> ExpectedL<Unit> {
            auto p = emitted.emplace(dep.spec, false);
            // Dependency resolution should have ensured that either every node exists OR an error should have been
            // logged to m_errors
            const auto& node = find_package(dep.spec).value_or_exit(VCPKG_LINE_INFO);

            // Evaluate the >=version constraint (if any)
            auto maybe_min = dep.dc.try_get_minimum_version();
            if (!node.second.overlay_or_override && maybe_min)
            {
                // Dependency resolution should have already logged any errors retrieving the scfl
                const auto& dep_scfl =
                    m_ver_provider.get_control_file({dep.spec.name(), *maybe_min.get()}).value_or_exit(VCPKG_LINE_INFO);
                const auto constraint_sver = dep_scfl.schemed_version();
                const auto selected_sver = node.second.scfl->schemed_version();
                auto r = compare_versions(selected_sver, constraint_sver);
                if (r == VerComp::unk)
                {
                    // In the error message, we report the baseline version instead of the "best selected" version to
                    // give the user simpler data to work with.
                    return format_incomparable_versions_message(
                        dep.spec, origin, node.second.baseline, constraint_sver);
                }
                Checks::check_exit(VCPKG_LINE_INFO,
                                   r != VerComp::lt,
                                   "Dependency resolution failed to consider a constraint. This is an internal error.");
            }

            // Evaluate feature constraints (if any)
            for (auto&& f : dep.features)
            {
                if (f == "core") continue;
                if (f == "default") continue;
                auto feature = node.second.scfl->source_control_file->find_feature(f);
                if (!feature)
                {
                    return msg::format_error(msgVersionMissingRequiredFeature,
                                             msg::version_spec =
                                                 Strings::concat(dep.spec.name(), '@', node.second.scfl->to_version()),
                                             msg::feature = f,
                                             msg::constraint_origin = origin);
                }
            }

            if (p.second)
            {
                // Newly inserted -> Add stack frame
                std::vector<DepSpec> deps;
                RequestType request = m_user_requested.find(dep.spec) != m_user_requested.end()
                                          ? RequestType::USER_REQUESTED
                                          : RequestType::AUTO_SELECTED;
                InstallPlanAction ipa(
                    dep.spec, *node.second.scfl, request, m_host_triplet, compute_feature_dependencies(node, deps), {});
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
            auto x = push(root, toplevel.name());
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
                ret.unsupported_features.insert({FeatureSpec(action.spec, "core"), supports_expr});
            }

            // Evaluate per-feature supports conditions
            for (auto&& fdeps : action.feature_dependencies)
            {
                if (fdeps.first == "core") continue;

                auto fpgh = scfl.source_control_file->find_feature(fdeps.first).value_or_exit(VCPKG_LINE_INFO);
                if (!fpgh.supports_expression.evaluate(vars))
                {
                    ret.unsupported_features.insert({FeatureSpec(action.spec, fdeps.first), supports_expr});
                }
            }
        }

        if (unsupported_port_action == UnsupportedPortAction::Error && !ret.unsupported_features.empty())
        {
            LocalizedString msg;
            for (auto&& f : ret.unsupported_features)
            {
                if (!msg.empty()) msg.append_raw("\n");

                const auto feature_spec = f.first.feature() == "core"
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

namespace vcpkg
{
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

        vpg.solve_with_roots(deps, toplevel);
        return vpg.finalize_extract_plan(toplevel, unsupported_port_action);
    }
}
