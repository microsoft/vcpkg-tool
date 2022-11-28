#include <catch2/catch.hpp>

#include <vcpkg/base/graphs.h>

#include <vcpkg/dependencies.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/triplet.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include <vcpkg-test/mockcmakevarprovider.h>
#include <vcpkg-test/util.h>

using namespace vcpkg;

using Test::make_control_file;
using Test::make_status_feature_pgh;
using Test::make_status_pgh;
using Test::MockCMakeVarProvider;
using Test::PackageSpecMap;

struct MockBaselineProvider : IBaselineProvider
{
    mutable std::map<std::string, Version, std::less<>> v;

    ExpectedL<Version> get_baseline_version(StringView name) const override
    {
        auto it = v.find(name);
        if (it == v.end()) return LocalizedString::from_raw("error");
        return it->second;
    }
};

struct MockVersionedPortfileProvider : IVersionedPortfileProvider
{
    mutable std::map<std::string, std::map<Version, SourceControlFileAndLocation, VersionMapLess>> v;

    ExpectedS<const SourceControlFileAndLocation&> get_control_file(
        const vcpkg::VersionSpec& versionspec) const override
    {
        return get_control_file(versionspec.port_name, versionspec.version);
    }

    ExpectedS<const SourceControlFileAndLocation&> get_control_file(const std::string& name,
                                                                    const vcpkg::Version& version) const
    {
        auto it = v.find(name);
        if (it == v.end()) return std::string("Unknown port name");
        auto it2 = it->second.find(version);
        if (it2 == it->second.end()) return std::string("Unknown port version");
        return it2->second;
    }

    virtual View<vcpkg::Version> get_port_versions(StringView) const override { Checks::unreachable(VCPKG_LINE_INFO); }

    SourceControlFileAndLocation& emplace(std::string&& name,
                                          Version&& version,
                                          VersionScheme scheme = VersionScheme::String)
    {
#if defined(__cpp_lib_map_try_emplace) && __cpp_lib_map_try_emplace >= 201411
        auto it = v.try_emplace(name).first;
#else // ^^^ has try_emplace / no try_emplace vvv
        auto it = v.find(name);
        if (it == v.end())
        {
            it = v.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple()).first;
        }
#endif

        auto it2 = it->second.find(version);
        if (it2 == it->second.end())
        {
            auto scf = std::make_unique<SourceControlFile>();
            auto core = std::make_unique<SourceParagraph>();
            core->name = name;
            core->raw_version = version.text();
            core->port_version = version.port_version();
            core->version_scheme = scheme;
            scf->core_paragraph = std::move(core);
            it2 = it->second.emplace(version, SourceControlFileAndLocation{std::move(scf), name}).first;
        }

        return it2->second;
    }

    virtual void load_all_control_files(std::map<std::string, const SourceControlFileAndLocation*>&) const override
    {
        Checks::unreachable(VCPKG_LINE_INFO);
    }
};

static void check_name_and_features(const InstallPlanAction& ipa,
                                    StringLiteral name,
                                    std::initializer_list<StringLiteral> features)
{
    CHECK(ipa.spec.name() == name);
    CHECK(ipa.source_control_file_and_location.has_value());
    {
        INFO("ipa.feature_list = [" << Strings::join(", ", ipa.feature_list) << "]");
        INFO("features = [" << Strings::join(", ", features) << "]");
        CHECK(ipa.feature_list.size() == features.size() + 1);
        for (auto&& f : features)
        {
            INFO("f = \"" << f.c_str() << "\"");
            CHECK(Util::find(ipa.feature_list, f) != ipa.feature_list.end());
        }
        CHECK(Util::find(ipa.feature_list, "core") != ipa.feature_list.end());
    }
}

static void check_name_and_version(const InstallPlanAction& ipa,
                                   StringLiteral name,
                                   Version v,
                                   std::initializer_list<StringLiteral> features = {})
{
    check_name_and_features(ipa, name, features);
    if (auto scfl = ipa.source_control_file_and_location.get())
    {
        CHECK(scfl->source_control_file->core_paragraph->raw_version == v.text());
        CHECK(scfl->source_control_file->core_paragraph->port_version == v.port_version());
    }
}

static void check_semver_version(const ExpectedL<DotVersion>& maybe_version,
                                 const std::string& version_string,
                                 const std::string& prerelease_string,
                                 uint64_t major,
                                 uint64_t minor,
                                 uint64_t patch,
                                 const std::vector<std::string>& identifiers)
{
    auto actual_version = maybe_version.value_or_exit(VCPKG_LINE_INFO);
    CHECK(actual_version.version_string == version_string);
    CHECK(actual_version.prerelease_string == prerelease_string);
    REQUIRE(actual_version.version.size() == 3);
    CHECK(actual_version.version[0] == major);
    CHECK(actual_version.version[1] == minor);
    CHECK(actual_version.version[2] == patch);
    CHECK(actual_version.identifiers == identifiers);
}

static void check_relaxed_version(const ExpectedL<DotVersion>& maybe_version,
                                  const std::vector<uint64_t>& version,
                                  const std::vector<std::string>& identifiers = {})
{
    auto actual_version = maybe_version.value_or_exit(VCPKG_LINE_INFO);
    CHECK(actual_version.version == version);
    CHECK(actual_version.identifiers == identifiers);
}

static void check_date_version(const ExpectedL<DateVersion>& maybe_version,
                               const std::string& version_string,
                               const std::vector<uint64_t>& identifiers)
{
    auto actual_version = maybe_version.value_or_exit(VCPKG_LINE_INFO);
    CHECK(actual_version.version_string == version_string);
    CHECK(actual_version.identifiers == identifiers);
}

static const PackageSpec& toplevel_spec()
{
    static const PackageSpec ret("toplevel-spec", Test::X86_WINDOWS);
    return ret;
}

struct MockOverlayProvider : IOverlayProvider
{
    MockOverlayProvider() = default;
    MockOverlayProvider(const MockOverlayProvider&) = delete;
    MockOverlayProvider& operator=(const MockOverlayProvider&) = delete;

    virtual Optional<const SourceControlFileAndLocation&> get_control_file(StringView name) const override
    {
        auto it = mappings.find(name);
        if (it == mappings.end())
        {
            return nullopt;
        }

        return it->second;
    }

    SourceControlFileAndLocation& emplace(const std::string& name,
                                          Version&& version,
                                          VersionScheme scheme = VersionScheme::String)
    {
        auto it = mappings.find(name);
        if (it == mappings.end())
        {
            auto scf = std::make_unique<SourceControlFile>();
            auto core = std::make_unique<SourceParagraph>();
            core->name = name;
            core->raw_version = version.text();
            core->port_version = version.port_version();
            core->version_scheme = scheme;
            scf->core_paragraph = std::move(core);
            it = mappings.emplace(name, SourceControlFileAndLocation{std::move(scf), name}).first;
        }
        return it->second;
    }

    SourceControlFileAndLocation& emplace(const std::string& name) { return emplace(name, {"1", 0}); }

    virtual void load_all_control_files(std::map<std::string, const SourceControlFileAndLocation*>&) const override
    {
        Checks::unreachable(VCPKG_LINE_INFO);
    }

private:
    std::map<std::string, SourceControlFileAndLocation, std::less<>> mappings;
};

static const MockOverlayProvider s_empty_mock_overlay;

static ExpectedS<ActionPlan> create_versioned_install_plan(const IVersionedPortfileProvider& provider,
                                                           const IBaselineProvider& bprovider,
                                                           const CMakeVars::CMakeVarProvider& var_provider,
                                                           const std::vector<Dependency>& deps,
                                                           const std::vector<DependencyOverride>& overrides,
                                                           const PackageSpec& toplevel)
{
    return create_versioned_install_plan(provider,
                                         bprovider,
                                         s_empty_mock_overlay,
                                         var_provider,
                                         deps,
                                         overrides,
                                         toplevel,
                                         Test::ARM_UWP,
                                         UnsupportedPortAction::Error);
}

static ExpectedS<ActionPlan> create_versioned_install_plan(const IVersionedPortfileProvider& provider,
                                                           const IBaselineProvider& bprovider,
                                                           const IOverlayProvider& oprovider,
                                                           const CMakeVars::CMakeVarProvider& var_provider,
                                                           const std::vector<Dependency>& deps,
                                                           const std::vector<DependencyOverride>& overrides,
                                                           const PackageSpec& toplevel)
{
    return create_versioned_install_plan(provider,
                                         bprovider,
                                         oprovider,
                                         var_provider,
                                         deps,
                                         overrides,
                                         toplevel,
                                         Test::ARM_UWP,
                                         UnsupportedPortAction::Error);
}

TEST_CASE ("basic version install single", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0});

    MockCMakeVarProvider var_provider;

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {{"a"}}, {}, toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 1);
    REQUIRE(install_plan.install_actions.at(0).spec.name() == "a");
}

TEST_CASE ("basic version install detect cycle", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0}).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{}},
    };
    vp.emplace("b", {"1", 0}).source_control_file->core_paragraph->dependencies = {
        Dependency{"a", {}, {}, DependencyConstraint{}},
    };

    MockCMakeVarProvider var_provider;

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {{"a"}}, {}, toplevel_spec());

    REQUIRE(!install_plan.has_value());
}

TEST_CASE ("basic version install scheme", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0}).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{}},
    };
    vp.emplace("b", {"1", 0});

    MockCMakeVarProvider var_provider;

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {{"a"}}, {}, toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    CHECK(install_plan.size() == 2);

    StringLiteral names[] = {"b", "a"};
    for (size_t i = 0; i < install_plan.install_actions.size() && i < 2; ++i)
    {
        CHECK(install_plan.install_actions[i].spec.name() == names[i]);
    }
}

TEST_CASE ("basic version install scheme diamond", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};
    bp.v["c"] = {"1", 0};
    bp.v["d"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0}).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{}},
        Dependency{"c", {}, {}, DependencyConstraint{}},
    };
    vp.emplace("b", {"1", 0}).source_control_file->core_paragraph->dependencies = {
        Dependency{"c", {}, {}, DependencyConstraint{}},
        Dependency{"d", {}, {}, DependencyConstraint{}},
    };
    vp.emplace("c", {"1", 0}).source_control_file->core_paragraph->dependencies = {
        Dependency{"d", {}, {}, DependencyConstraint{}},
    };
    vp.emplace("d", {"1", 0});

    MockCMakeVarProvider var_provider;

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {{"a"}}, {}, toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    CHECK(install_plan.size() == 4);

    StringLiteral names[] = {"d", "c", "b", "a"};
    for (size_t i = 0; i < install_plan.install_actions.size() && i < 4; ++i)
    {
        CHECK(install_plan.install_actions[i].spec.name() == names[i]);
    }
}

TEST_CASE ("basic version install scheme baseline missing", "[versionplan]")
{
    MockBaselineProvider bp;

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0});

    MockCMakeVarProvider var_provider;

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {{"a"}}, {}, toplevel_spec());

    REQUIRE(!install_plan.has_value());
}

TEST_CASE ("basic version install scheme baseline missing success", "[versionplan]")
{
    MockBaselineProvider bp;

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0});
    vp.emplace("a", {"2", 0});
    vp.emplace("a", {"3", 0});

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "2"}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 1);
    check_name_and_version(install_plan.install_actions[0], "a", {"2", 0});
}

TEST_CASE ("basic version install scheme baseline", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0});
    vp.emplace("a", {"2", 0});
    vp.emplace("a", {"3", 0});

    MockCMakeVarProvider var_provider;

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {{"a"}}, {}, toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 1);
    check_name_and_version(install_plan.install_actions[0], "a", {"2", 0});
}

TEST_CASE ("version string baseline agree", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0});
    vp.emplace("a", {"2", 0});
    vp.emplace("a", {"3", 0});

    MockCMakeVarProvider var_provider;

    auto install_plan = create_versioned_install_plan(
        vp, bp, var_provider, {Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "2"}}}, {}, toplevel_spec());

    REQUIRE(install_plan.has_value());
}

TEST_CASE ("version install scheme baseline conflict", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0});
    vp.emplace("a", {"2", 0});
    vp.emplace("a", {"3", 0});

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "3"}},
                                      },
                                      {},
                                      toplevel_spec());

    REQUIRE(!install_plan.has_value());
}

TEST_CASE ("version install string port version", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2", 0});
    vp.emplace("a", {"2", 1});
    vp.emplace("a", {"2", 2});

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "2", 1}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 1);
    check_name_and_version(install_plan.install_actions[0], "a", {"2", 1});
}

TEST_CASE ("version install string port version 2", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2", 1};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2", 0});
    vp.emplace("a", {"2", 1});
    vp.emplace("a", {"2", 2});

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "2", 0}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 1);
    check_name_and_version(install_plan.install_actions[0], "a", {"2", 1});
    CHECK(install_plan.install_actions[0].request_type == RequestType::USER_REQUESTED);
}

TEST_CASE ("version install transitive string", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2", 0}).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "1"}},
    };
    vp.emplace("a", {"2", 1}).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "2"}},
    };
    vp.emplace("b", {"1", 0});
    vp.emplace("b", {"2", 0});

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "2", 1}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 2);
    check_name_and_version(install_plan.install_actions[0], "b", {"2", 0});
    CHECK(install_plan.install_actions[0].request_type == RequestType::AUTO_SELECTED);
    check_name_and_version(install_plan.install_actions[1], "a", {"2", 1});
    CHECK(install_plan.install_actions[1].request_type == RequestType::USER_REQUESTED);
}

TEST_CASE ("version install simple relaxed", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2", 0}, VersionScheme::Relaxed);
    vp.emplace("a", {"3", 0}, VersionScheme::Relaxed);

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "3", 0}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 1);
    check_name_and_version(install_plan.install_actions[0], "a", {"3", 0});
}

TEST_CASE ("version install transitive relaxed", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2", 0};
    bp.v["b"] = {"2", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2", 0}, VersionScheme::Relaxed);
    vp.emplace("a", {"3", 0}, VersionScheme::Relaxed).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "3"}},
    };
    vp.emplace("b", {"2", 0}, VersionScheme::Relaxed);
    vp.emplace("b", {"3", 0}, VersionScheme::Relaxed);

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "3", 0}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 2);
    check_name_and_version(install_plan.install_actions[0], "b", {"3", 0});
    check_name_and_version(install_plan.install_actions[1], "a", {"3", 0});
}

TEST_CASE ("version install diamond relaxed", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2", 0};
    bp.v["b"] = {"3", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2", 0}, VersionScheme::Relaxed);
    vp.emplace("a", {"3", 0}, VersionScheme::Relaxed).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "2", 1}},
        Dependency{"c", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "5", 1}},
    };
    vp.emplace("b", {"2", 1}, VersionScheme::Relaxed);
    vp.emplace("b", {"3", 0}, VersionScheme::Relaxed).source_control_file->core_paragraph->dependencies = {
        Dependency{"c", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "9", 2}},
    };
    vp.emplace("c", {"5", 1}, VersionScheme::Relaxed);
    vp.emplace("c", {"9", 2}, VersionScheme::Relaxed);

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "3", 0}},
                                          Dependency{"b", {}, {}, {VersionConstraintKind::Minimum, "2", 1}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 3);
    check_name_and_version(install_plan.install_actions[0], "c", {"9", 2});
    check_name_and_version(install_plan.install_actions[1], "b", {"3", 0});
    check_name_and_version(install_plan.install_actions[2], "a", {"3", 0});
}

TEST_CASE ("version parse semver", "[versionplan]")
{
    check_semver_version(DotVersion::try_parse_semver("1.2.3"), "1.2.3", "", 1, 2, 3, {});
    check_semver_version(DotVersion::try_parse_semver("1.0.0-alpha"), "1.0.0", "alpha", 1, 0, 0, {"alpha"});
    check_semver_version(DotVersion::try_parse_semver("1.0.0-0alpha0"), "1.0.0", "0alpha0", 1, 0, 0, {"0alpha0"});
    check_semver_version(
        DotVersion::try_parse_semver("1.0.0-alpha.1.0.0"), "1.0.0", "alpha.1.0.0", 1, 0, 0, {"alpha", "1", "0", "0"});
    check_semver_version(DotVersion::try_parse_semver("1.0.0-alpha.1.x.y.z.0-alpha.0-beta.l-a-s-t"),
                         "1.0.0",
                         "alpha.1.x.y.z.0-alpha.0-beta.l-a-s-t",
                         1,
                         0,
                         0,
                         {"alpha", "1", "x", "y", "z", "0-alpha", "0-beta", "l-a-s-t"});
    check_semver_version(DotVersion::try_parse_semver("1.0.0----------------------------------"),
                         "1.0.0",
                         "---------------------------------",
                         1,
                         0,
                         0,
                         {"---------------------------------"});
    check_semver_version(DotVersion::try_parse_semver("1.0.0+build"), "1.0.0", "", 1, 0, 0, {});
    check_semver_version(DotVersion::try_parse_semver("1.0.0-alpha+build"), "1.0.0", "alpha", 1, 0, 0, {"alpha"});
    check_semver_version(DotVersion::try_parse_semver("1.0.0-alpha+build.ok"), "1.0.0", "alpha", 1, 0, 0, {"alpha"});
    check_semver_version(
        DotVersion::try_parse_semver("1.0.0-alpha+build.ok-too"), "1.0.0", "alpha", 1, 0, 0, {"alpha"});

    CHECK(!DotVersion::try_parse_semver("1.0").has_value());
    CHECK(!DotVersion::try_parse_semver("1.0-alpha").has_value());
    CHECK(!DotVersion::try_parse_semver("1.0.0.0").has_value());
    CHECK(!DotVersion::try_parse_semver("1.02.03").has_value());
    CHECK(!DotVersion::try_parse_semver("1.0.0-").has_value());
    CHECK(!DotVersion::try_parse_semver("1.0.0-01").has_value());
    CHECK(!DotVersion::try_parse_semver("1.0.0-alpha#2").has_value());
    CHECK(!DotVersion::try_parse_semver("1.0.0-alpha+build+notok").has_value());
}

TEST_CASE ("version parse relaxed", "[versionplan]")
{
    check_relaxed_version(DotVersion::try_parse_relaxed("1.2.3"), {1, 2, 3});
    check_relaxed_version(DotVersion::try_parse_relaxed("1"), {1});
    check_relaxed_version(
        DotVersion::try_parse_relaxed("1.20.300.4000.50000.6000000.70000000.80000000.18446744073709551610"),
        {1, 20, 300, 4000, 50000, 6000000, 70000000, 80000000, 18446744073709551610u});
    check_relaxed_version(DotVersion::try_parse_relaxed("1.0.0.0-alpha"), {1, 0, 0, 0}, {"alpha"});
    check_relaxed_version(DotVersion::try_parse_relaxed("1.0.0.0-alpha-0.1"), {1, 0, 0, 0}, {"alpha-0", "1"});
    check_relaxed_version(DotVersion::try_parse_relaxed("1.0.0.0-alpha+build-ok"), {1, 0, 0, 0}, {"alpha"});

    CHECK(!DotVersion::try_parse_relaxed("1.1a.2").has_value());
    CHECK(!DotVersion::try_parse_relaxed("01.002.003").has_value());
    CHECK(!DotVersion::try_parse_relaxed("1.0.0-").has_value());
    CHECK(!DotVersion::try_parse_relaxed("1.0.0+extra+other").has_value());
}

TEST_CASE ("version parse date", "[versionplan]")
{
    check_date_version(DateVersion::try_parse("2020-12-25"), "2020-12-25", {});
    check_date_version(DateVersion::try_parse("2020-12-25.1.2.3"), "2020-12-25", {1, 2, 3});

    CHECK(!DateVersion::try_parse("2020-1-1").has_value());
    CHECK(!DateVersion::try_parse("2020-01-01.alpha").has_value());
    CHECK(!DateVersion::try_parse("2020-01-01.2a").has_value());
    CHECK(!DateVersion::try_parse("2020-01-01.01").has_value());
}

TEST_CASE ("version sort semver", "[versionplan]")
{
    std::vector<DotVersion> versions{
        DotVersion::try_parse_semver("1.0.0").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("0.0.0").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.1.0").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("2.0.0").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.1.1").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.0.1").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.0.0-alpha.1").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.0.0-beta").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.0.0-alpha").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.0.0-alpha.beta").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.0.0-rc").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.0.0-beta.2").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.0.0-beta.20").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.0.0-beta.3").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.0.0-1").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_semver("1.0.0-0alpha").value_or_exit(VCPKG_LINE_INFO),
    };

    std::sort(std::begin(versions), std::end(versions));

    CHECK(versions[0].original_string == "0.0.0");
    CHECK(versions[1].original_string == "1.0.0-1");
    CHECK(versions[2].original_string == "1.0.0-0alpha");
    CHECK(versions[3].original_string == "1.0.0-alpha");
    CHECK(versions[4].original_string == "1.0.0-alpha.1");
    CHECK(versions[5].original_string == "1.0.0-alpha.beta");
    CHECK(versions[6].original_string == "1.0.0-beta");
    CHECK(versions[7].original_string == "1.0.0-beta.2");
    CHECK(versions[8].original_string == "1.0.0-beta.3");
    CHECK(versions[9].original_string == "1.0.0-beta.20");
    CHECK(versions[10].original_string == "1.0.0-rc");
    CHECK(versions[11].original_string == "1.0.0");
    CHECK(versions[12].original_string == "1.0.1");
    CHECK(versions[13].original_string == "1.1.0");
    CHECK(versions[14].original_string == "1.1.1");
    CHECK(versions[15].original_string == "2.0.0");
}

TEST_CASE ("version sort relaxed", "[versionplan]")
{
    std::vector<DotVersion> versions{
        DotVersion::try_parse_relaxed("2.1-alpha.alpha").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("1.0.0").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("2.0-1").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("1.0").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("1").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("2.1-alpha").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("2").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("1.1").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("1.10.1").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("2.0-0").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("1.0.1").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("2.1-beta").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("1.0.0.1").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("1.0.0.2").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("2.0").value_or_exit(VCPKG_LINE_INFO),
        DotVersion::try_parse_relaxed("2.0-rc").value_or_exit(VCPKG_LINE_INFO),
    };

    std::sort(std::begin(versions), std::end(versions));

    CHECK(versions[0].original_string == "1");
    CHECK(versions[1].original_string == "1.0");
    CHECK(versions[2].original_string == "1.0.0");
    CHECK(versions[3].original_string == "1.0.0.1");
    CHECK(versions[4].original_string == "1.0.0.2");
    CHECK(versions[5].original_string == "1.0.1");
    CHECK(versions[6].original_string == "1.1");
    CHECK(versions[7].original_string == "1.10.1");
    CHECK(versions[8].original_string == "2");
    CHECK(versions[9].original_string == "2.0-0");
    CHECK(versions[10].original_string == "2.0-1");
    CHECK(versions[11].original_string == "2.0-rc");
    CHECK(versions[12].original_string == "2.0");
    CHECK(versions[13].original_string == "2.1-alpha");
    CHECK(versions[14].original_string == "2.1-alpha.alpha");
    CHECK(versions[15].original_string == "2.1-beta");
}

TEST_CASE ("version sort date", "[versionplan]")
{
    std::vector<DateVersion> versions{
        DateVersion::try_parse("2021-01-01.2").value_or_exit(VCPKG_LINE_INFO),
        DateVersion::try_parse("2021-01-01.1").value_or_exit(VCPKG_LINE_INFO),
        DateVersion::try_parse("2021-01-01.1.1").value_or_exit(VCPKG_LINE_INFO),
        DateVersion::try_parse("2021-01-01.1.0").value_or_exit(VCPKG_LINE_INFO),
        DateVersion::try_parse("2021-01-01").value_or_exit(VCPKG_LINE_INFO),
        DateVersion::try_parse("2021-01-01").value_or_exit(VCPKG_LINE_INFO),
        DateVersion::try_parse("2020-12-25").value_or_exit(VCPKG_LINE_INFO),
        DateVersion::try_parse("2020-12-31").value_or_exit(VCPKG_LINE_INFO),
        DateVersion::try_parse("2021-01-01.10").value_or_exit(VCPKG_LINE_INFO),
    };

    std::sort(std::begin(versions), std::end(versions));

    CHECK(versions[0].original_string == "2020-12-25");
    CHECK(versions[1].original_string == "2020-12-31");
    CHECK(versions[2].original_string == "2021-01-01");
    CHECK(versions[3].original_string == "2021-01-01");
    CHECK(versions[4].original_string == "2021-01-01.1");
    CHECK(versions[5].original_string == "2021-01-01.1.0");
    CHECK(versions[6].original_string == "2021-01-01.1.1");
    CHECK(versions[7].original_string == "2021-01-01.2");
    CHECK(versions[8].original_string == "2021-01-01.10");
}

TEST_CASE ("version compare string", "[versionplan]")
{
    const Version a_0("a", 0);
    const Version a_1("a", 1);
    const Version b_1("b", 1);
    CHECK(VerComp::lt == compare_versions(VersionScheme::String, a_0, VersionScheme::String, a_1));
    CHECK(VerComp::eq == compare_versions(VersionScheme::String, a_0, VersionScheme::String, a_0));
    CHECK(VerComp::gt == compare_versions(VersionScheme::String, a_1, VersionScheme::String, a_0));
    CHECK(VerComp::unk == compare_versions(VersionScheme::String, a_1, VersionScheme::String, b_1));
}

TEST_CASE ("version compare_any", "[versionplan]")
{
    const Version a_0("a", 0);
    const Version a_1("a", 1);
    const Version b_1("b", 1);
    CHECK(VerComp::lt == compare_any(a_0, a_1));
    CHECK(VerComp::gt == compare_any(a_1, a_0));
    CHECK(VerComp::eq == compare_any(a_0, a_0));
    CHECK(VerComp::unk == compare_any(a_1, b_1));

    const Version v_0_0("0", 0);
    const Version v_1_0("1", 0);
    const Version v_1_1_1("1.1", 1);
    CHECK(VerComp::lt == compare_any(v_0_0, v_1_0));
    CHECK(VerComp::gt == compare_any(v_1_1_1, v_1_0));
    CHECK(VerComp::eq == compare_any(v_0_0, v_0_0));

    const Version date_0("2021-04-05", 0);
    const Version date_1("2022-02-01", 0);
    CHECK(VerComp::eq == compare_any(date_0, date_0));
    CHECK(VerComp::lt == compare_any(date_0, date_1));

    CHECK(VerComp::unk == compare_any(date_0, a_0));
    // Note: dates are valid relaxed dotversions, so these are valid comparisons
    CHECK(VerComp::gt == compare_any(date_0, v_0_0));
    CHECK(VerComp::gt == compare_any(date_0, v_1_1_1));
}

TEST_CASE ("version install simple semver", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2.0.0", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2.0.0", 0}, VersionScheme::Semver);
    vp.emplace("a", {"3.0.0", 0}, VersionScheme::Semver);

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "3.0.0", 0}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 1);
    check_name_and_version(install_plan.install_actions[0], "a", {"3.0.0", 0});
}

TEST_CASE ("version install transitive semver", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2.0.0", 0};
    bp.v["b"] = {"2.0.0", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2.0.0", 0}, VersionScheme::Semver);
    vp.emplace("a", {"3.0.0", 0}, VersionScheme::Semver).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "3.0.0"}},
    };
    vp.emplace("b", {"2.0.0", 0}, VersionScheme::Semver);
    vp.emplace("b", {"3.0.0", 0}, VersionScheme::Semver);

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "3.0.0", 0}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 2);
    check_name_and_version(install_plan.install_actions[0], "b", {"3.0.0", 0});
    check_name_and_version(install_plan.install_actions[1], "a", {"3.0.0", 0});
}

TEST_CASE ("version install diamond semver", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2.0.0", 0};
    bp.v["b"] = {"3.0.0", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2.0.0", 0}, VersionScheme::Semver);
    vp.emplace("a", {"3.0.0", 0}, VersionScheme::Semver).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "2.0.0", 1}},
        Dependency{"c", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "5.0.0", 1}},
    };
    vp.emplace("b", {"2.0.0", 1}, VersionScheme::Semver);
    vp.emplace("b", {"3.0.0", 0}, VersionScheme::Semver).source_control_file->core_paragraph->dependencies = {
        Dependency{"c", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "9.0.0", 2}},
    };
    vp.emplace("c", {"5.0.0", 1}, VersionScheme::Semver);
    vp.emplace("c", {"9.0.0", 2}, VersionScheme::Semver);

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "3.0.0", 0}},
                                          Dependency{"b", {}, {}, {VersionConstraintKind::Minimum, "2.0.0", 1}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 3);
    check_name_and_version(install_plan.install_actions[0], "c", {"9.0.0", 2});
    check_name_and_version(install_plan.install_actions[1], "b", {"3.0.0", 0});
    check_name_and_version(install_plan.install_actions[2], "a", {"3.0.0", 0});
}

TEST_CASE ("version install simple date", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2020-02-01", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2020-02-01", 0}, VersionScheme::Date);
    vp.emplace("a", {"2020-03-01", 0}, VersionScheme::Date);

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "2020-03-01", 0}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 1);
    check_name_and_version(install_plan.install_actions[0], "a", {"2020-03-01", 0});
}

TEST_CASE ("version install transitive date", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2020-01-01.2", 0};
    bp.v["b"] = {"2020-01-01.3", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2020-01-01.2", 0}, VersionScheme::Date);
    vp.emplace("a", {"2020-01-01.3", 0}, VersionScheme::Date).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "2020-01-01.3"}},
    };
    vp.emplace("b", {"2020-01-01.2", 0}, VersionScheme::Date);
    vp.emplace("b", {"2020-01-01.3", 0}, VersionScheme::Date);

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "2020-01-01.3", 0}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 2);
    check_name_and_version(install_plan.install_actions[0], "b", {"2020-01-01.3", 0});
    check_name_and_version(install_plan.install_actions[1], "a", {"2020-01-01.3", 0});
}

TEST_CASE ("version install diamond date", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"2020-01-02", 0};
    bp.v["b"] = {"2020-01-03", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2020-01-02", 0}, VersionScheme::Date);
    vp.emplace("a", {"2020-01-03", 0}, VersionScheme::Date).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "2020-01-02", 1}},
        Dependency{"c", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "2020-01-05", 1}},
    };
    vp.emplace("b", {"2020-01-02", 1}, VersionScheme::Date);
    vp.emplace("b", {"2020-01-03", 0}, VersionScheme::Date).source_control_file->core_paragraph->dependencies = {
        Dependency{"c", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "2020-01-09", 2}},
    };
    vp.emplace("c", {"2020-01-05", 1}, VersionScheme::Date);
    vp.emplace("c", {"2020-01-09", 2}, VersionScheme::Date);

    MockCMakeVarProvider var_provider;

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "2020-01-03", 0}},
                                          Dependency{"b", {}, {}, {VersionConstraintKind::Minimum, "2020-01-02", 1}},
                                      },
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 3);
    check_name_and_version(install_plan.install_actions[0], "c", {"2020-01-09", 2});
    check_name_and_version(install_plan.install_actions[1], "b", {"2020-01-03", 0});
    check_name_and_version(install_plan.install_actions[2], "a", {"2020-01-03", 0});
}

static void CHECK_LINES(const std::string& a, const std::string& b)
{
    auto as = Strings::split(a, '\n');
    auto bs = Strings::split(b, '\n');
    for (size_t i = 0; i < as.size() && i < bs.size(); ++i)
    {
        INFO(i);
        CHECK(as[i] == bs[i]);
    }
    CHECK(as.size() == bs.size());
}

TEST_CASE ("version install scheme failure", "[versionplan]")
{
    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1.0.0", 0}, VersionScheme::Semver);
    vp.emplace("a", {"1.0.1", 0}, VersionScheme::String);
    vp.emplace("a", {"1.0.2", 0}, VersionScheme::Semver);

    MockCMakeVarProvider var_provider;

    SECTION ("lower baseline")
    {
        MockBaselineProvider bp;
        bp.v["a"] = {"1.0.0", 0};

        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          var_provider,
                                          {Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "1.0.1", 0}}},
                                          {},
                                          toplevel_spec());

        REQUIRE(!install_plan.error().empty());
        CHECK_LINES(
            install_plan.error(),
            R"(Error: Version conflict on a:x86-windows: baseline required 1.0.0 but vcpkg could not compare it to 1.0.1

The two versions used incomparable schemes:
    "1.0.1" was of scheme string
    "1.0.0" was of scheme semver

This can be resolved by adding an explicit override to the preferred version, for example:

    "overrides": [
        { "name": "a", "version": "1.0.1" }
    ]

See `vcpkg help versioning` for more information.)");
    }
    SECTION ("higher baseline")
    {
        MockBaselineProvider bp;
        bp.v["a"] = {"1.0.2", 0};

        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          var_provider,
                                          {Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "1.0.1", 0}}},
                                          {},
                                          toplevel_spec());

        REQUIRE(!install_plan.error().empty());
        CHECK_LINES(
            install_plan.error(),
            R"(Error: Version conflict on a:x86-windows: baseline required 1.0.2 but vcpkg could not compare it to 1.0.1

The two versions used incomparable schemes:
    "1.0.1" was of scheme string
    "1.0.2" was of scheme semver

This can be resolved by adding an explicit override to the preferred version, for example:

    "overrides": [
        { "name": "a", "version": "1.0.1" }
    ]

See `vcpkg help versioning` for more information.)");
    }
}

TEST_CASE ("version install relaxed cross with semver success", "[versionplan]")
{
    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1.0.0", 0}, VersionScheme::Semver);
    vp.emplace("a", {"1.0.1", 0}, VersionScheme::Relaxed);
    vp.emplace("a", {"1.0.2", 0}, VersionScheme::Semver);

    MockCMakeVarProvider var_provider;

    SECTION ("lower baseline")
    {
        MockBaselineProvider bp;
        bp.v["a"] = {"1.0.0", 0};

        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          var_provider,
                                          {Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "1.0.1", 0}}},
                                          {},
                                          toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        check_name_and_version(install_plan.install_actions[0], "a", {"1.0.1", 0});
    }
    SECTION ("higher baseline")
    {
        MockBaselineProvider bp;
        bp.v["a"] = {"1.0.2", 0};

        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          var_provider,
                                          {Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "1.0.1", 0}}},
                                          {},
                                          toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        check_name_and_version(install_plan.install_actions[0], "a", {"1.0.2", 0});
    }
}

TEST_CASE ("version install scheme change in port version", "[versionplan]")
{
    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"2", 0}).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "1"}},
    };
    vp.emplace("a", {"2", 1}).source_control_file->core_paragraph->dependencies = {
        Dependency{"b", {}, {}, DependencyConstraint{VersionConstraintKind::Minimum, "1", 1}},
    };
    vp.emplace("b", {"1", 0}, VersionScheme::String);
    vp.emplace("b", {"1", 1}, VersionScheme::Relaxed);

    MockCMakeVarProvider var_provider;

    SECTION ("lower baseline")
    {
        MockBaselineProvider bp;
        bp.v["a"] = {"2", 0};

        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          var_provider,
                                          {
                                              Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "2", 1}},
                                          },
                                          {},
                                          toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 2);
        check_name_and_version(install_plan.install_actions[0], "b", {"1", 1});
        check_name_and_version(install_plan.install_actions[1], "a", {"2", 1});
    }
    SECTION ("higher baseline")
    {
        MockBaselineProvider bp;
        bp.v["a"] = {"2", 1};

        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          var_provider,
                                          {
                                              Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "2", 0}},
                                          },
                                          {},
                                          toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 2);
        check_name_and_version(install_plan.install_actions[0], "b", {"1", 1});
        check_name_and_version(install_plan.install_actions[1], "a", {"2", 1});
    }
}

TEST_CASE ("version install simple feature", "[versionplan]")
{
    MockVersionedPortfileProvider vp;
    {
        auto a_x = std::make_unique<FeatureParagraph>();
        a_x->name = "x";
        vp.emplace("a", {"1", 0}, VersionScheme::Relaxed)
            .source_control_file->feature_paragraphs.push_back(std::move(a_x));
    }
    {
        auto a_x = std::make_unique<FeatureParagraph>();
        a_x->name = "x";
        vp.emplace("semver", {"1.0.0", 0}, VersionScheme::Semver)
            .source_control_file->feature_paragraphs.push_back(std::move(a_x));
    }
    {
        auto a_x = std::make_unique<FeatureParagraph>();
        a_x->name = "x";
        vp.emplace("date", {"2020-01-01", 0}, VersionScheme::Date)
            .source_control_file->feature_paragraphs.push_back(std::move(a_x));
    }

    MockCMakeVarProvider var_provider;

    SECTION ("with baseline")
    {
        MockBaselineProvider bp;
        bp.v["a"] = {"1", 0};
        bp.v["semver"] = {"1.0.0", 0};
        bp.v["date"] = {"2020-01-01", 0};

        SECTION ("relaxed")
        {
            auto install_plan = create_versioned_install_plan(vp,
                                                              bp,
                                                              var_provider,
                                                              {
                                                                  Dependency{"a", {"x"}},
                                                              },
                                                              {},
                                                              toplevel_spec())
                                    .value_or_exit(VCPKG_LINE_INFO);

            REQUIRE(install_plan.size() == 1);
            check_name_and_version(install_plan.install_actions[0], "a", {"1", 0}, {"x"});
        }
        SECTION ("semver")
        {
            auto install_plan = create_versioned_install_plan(vp,
                                                              bp,
                                                              var_provider,
                                                              {
                                                                  Dependency{"semver", {"x"}},
                                                              },
                                                              {},
                                                              toplevel_spec())
                                    .value_or_exit(VCPKG_LINE_INFO);

            REQUIRE(install_plan.size() == 1);
            check_name_and_version(install_plan.install_actions[0], "semver", {"1.0.0", 0}, {"x"});
        }
        SECTION ("date")
        {
            auto install_plan = create_versioned_install_plan(vp,
                                                              bp,
                                                              var_provider,
                                                              {
                                                                  Dependency{"date", {"x"}},
                                                              },
                                                              {},
                                                              toplevel_spec())
                                    .value_or_exit(VCPKG_LINE_INFO);

            REQUIRE(install_plan.size() == 1);
            check_name_and_version(install_plan.install_actions[0], "date", {"2020-01-01", 0}, {"x"});
        }
    }

    SECTION ("without baseline")
    {
        MockBaselineProvider bp;

        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          var_provider,
                                          {
                                              Dependency{"a", {"x"}, {}, {VersionConstraintKind::Minimum, "1", 0}},
                                          },
                                          {},
                                          toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 1);
        check_name_and_version(install_plan.install_actions[0], "a", {"1", 0}, {"x"});
    }
}

static std::unique_ptr<FeatureParagraph> make_fpgh(std::string name)
{
    auto f = std::make_unique<FeatureParagraph>();
    f->name = std::move(name);
    return f;
}

TEST_CASE ("version install transitive features", "[versionplan]")
{
    MockVersionedPortfileProvider vp;

    auto a_x = make_fpgh("x");
    a_x->dependencies.push_back(Dependency{"b", {"y"}});
    vp.emplace("a", {"1", 0}, VersionScheme::Relaxed).source_control_file->feature_paragraphs.push_back(std::move(a_x));

    auto b_y = make_fpgh("y");
    vp.emplace("b", {"1", 0}, VersionScheme::Relaxed).source_control_file->feature_paragraphs.push_back(std::move(b_y));

    MockCMakeVarProvider var_provider;

    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};

    auto install_plan = create_versioned_install_plan(vp,
                                                      bp,
                                                      var_provider,
                                                      {
                                                          Dependency{"a", {"x"}},
                                                      },
                                                      {},
                                                      toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 2);
    check_name_and_version(install_plan.install_actions[0], "b", {"1", 0}, {"y"});
    check_name_and_version(install_plan.install_actions[1], "a", {"1", 0}, {"x"});
}

TEST_CASE ("version install transitive feature versioned", "[versionplan]")
{
    MockVersionedPortfileProvider vp;

    auto a_x = make_fpgh("x");
    a_x->dependencies.push_back(Dependency{"b", {"y"}, {}, {VersionConstraintKind::Minimum, "2", 0}});
    vp.emplace("a", {"1", 0}, VersionScheme::Relaxed).source_control_file->feature_paragraphs.push_back(std::move(a_x));

    {
        auto b_y = make_fpgh("y");
        vp.emplace("b", {"1", 0}, VersionScheme::Relaxed)
            .source_control_file->feature_paragraphs.push_back(std::move(b_y));
    }
    {
        auto b_y = make_fpgh("y");
        b_y->dependencies.push_back(Dependency{"c"});
        vp.emplace("b", {"2", 0}, VersionScheme::Relaxed)
            .source_control_file->feature_paragraphs.push_back(std::move(b_y));
    }

    vp.emplace("c", {"1", 0}, VersionScheme::Relaxed);

    MockCMakeVarProvider var_provider;

    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["c"] = {"1", 0};

    auto install_plan = create_versioned_install_plan(vp,
                                                      bp,
                                                      var_provider,
                                                      {
                                                          Dependency{"a", {"x"}},
                                                      },
                                                      {},
                                                      toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 3);
    check_name_and_version(install_plan.install_actions[0], "c", {"1", 0});
    CHECK(install_plan.install_actions[0].request_type == RequestType::AUTO_SELECTED);
    check_name_and_version(install_plan.install_actions[1], "b", {"2", 0}, {"y"});
    CHECK(install_plan.install_actions[1].request_type == RequestType::AUTO_SELECTED);
    check_name_and_version(install_plan.install_actions[2], "a", {"1", 0}, {"x"});
    CHECK(install_plan.install_actions[2].request_type == RequestType::USER_REQUESTED);
}

TEST_CASE ("version install constraint-reduction", "[versionplan]")
{
    MockCMakeVarProvider var_provider;

    SECTION ("higher baseline")
    {
        MockVersionedPortfileProvider vp;

        vp.emplace("b", {"1", 0}, VersionScheme::Relaxed).source_control_file->core_paragraph->dependencies = {
            Dependency{"c", {}, {}, {VersionConstraintKind::Minimum, "2"}},
        };
        vp.emplace("b", {"2", 0}, VersionScheme::Relaxed).source_control_file->core_paragraph->dependencies = {
            Dependency{"c", {}, {}, {VersionConstraintKind::Minimum, "1"}},
        };

        vp.emplace("c", {"1", 0}, VersionScheme::Relaxed);
        // c@2 is used to detect if certain constraints were evaluated
        vp.emplace("c", {"2", 0}, VersionScheme::Relaxed);

        MockBaselineProvider bp;
        bp.v["b"] = {"2", 0};
        bp.v["c"] = {"1", 0};

        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          var_provider,
                                          {
                                              Dependency{"b", {}, {}, {VersionConstraintKind::Minimum, "1"}},
                                          },
                                          {},
                                          toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 2);
        check_name_and_version(install_plan.install_actions[0], "c", {"1", 0});
        check_name_and_version(install_plan.install_actions[1], "b", {"2", 0});
    }

    SECTION ("higher toplevel")
    {
        MockVersionedPortfileProvider vp;

        vp.emplace("b", {"1", 0}, VersionScheme::Relaxed).source_control_file->core_paragraph->dependencies = {
            Dependency{"c", {}, {}, {VersionConstraintKind::Minimum, "2"}},
        };
        vp.emplace("b", {"2", 0}, VersionScheme::Relaxed).source_control_file->core_paragraph->dependencies = {
            Dependency{"c", {}, {}, {VersionConstraintKind::Minimum, "1"}},
        };

        vp.emplace("c", {"1", 0}, VersionScheme::Relaxed);
        // c@2 is used to detect if certain constraints were evaluated
        vp.emplace("c", {"2", 0}, VersionScheme::Relaxed);

        MockBaselineProvider bp;
        bp.v["b"] = {"1", 0};
        bp.v["c"] = {"1", 0};

        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          var_provider,
                                          {
                                              Dependency{"b", {}, {}, {VersionConstraintKind::Minimum, "2"}},
                                          },
                                          {},
                                          toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 2);
        check_name_and_version(install_plan.install_actions[0], "c", {"1", 0});
        check_name_and_version(install_plan.install_actions[1], "b", {"2", 0});
    }
}

TEST_CASE ("version install overrides", "[versionplan]")
{
    MockCMakeVarProvider var_provider;

    MockVersionedPortfileProvider vp;

    vp.emplace("b", {"1", 0}, VersionScheme::Relaxed);
    vp.emplace("b", {"2", 0}, VersionScheme::Relaxed);
    vp.emplace("c", {"1", 0}, VersionScheme::String);
    vp.emplace("c", {"2", 0}, VersionScheme::String);

    MockBaselineProvider bp;
    bp.v["b"] = {"2", 0};
    bp.v["c"] = {"2", 0};

    SECTION ("string")
    {
        auto install_plan = create_versioned_install_plan(vp,
                                                          bp,
                                                          var_provider,
                                                          {Dependency{"c"}},
                                                          {DependencyOverride{"b", "1"}, DependencyOverride{"c", "1"}},
                                                          toplevel_spec())
                                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 1);
        check_name_and_version(install_plan.install_actions[0], "c", {"1", 0});
    }

    SECTION ("relaxed")
    {
        auto install_plan = create_versioned_install_plan(vp,
                                                          bp,
                                                          var_provider,
                                                          {Dependency{"b"}},
                                                          {DependencyOverride{"b", "1"}, DependencyOverride{"c", "1"}},
                                                          toplevel_spec())
                                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 1);
        check_name_and_version(install_plan.install_actions[0], "b", {"1", 0});
    }
}

TEST_CASE ("version install transitive overrides", "[versionplan]")
{
    MockCMakeVarProvider var_provider;

    MockVersionedPortfileProvider vp;

    vp.emplace("b", {"1", 0}, VersionScheme::Relaxed)
        .source_control_file->core_paragraph->dependencies.push_back(
            {"c", {}, {}, {VersionConstraintKind::Minimum, "2", 1}});
    vp.emplace("b", {"2", 0}, VersionScheme::Relaxed);
    vp.emplace("c", {"1", 0}, VersionScheme::String);
    vp.emplace("c", {"2", 1}, VersionScheme::String);

    MockBaselineProvider bp;
    bp.v["b"] = {"2", 0};
    bp.v["c"] = {"2", 1};

    auto install_plan = create_versioned_install_plan(vp,
                                                      bp,
                                                      var_provider,
                                                      {Dependency{"b"}},
                                                      {DependencyOverride{"b", "1"}, DependencyOverride{"c", "1"}},
                                                      toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 2);
    check_name_and_version(install_plan.install_actions[0], "c", {"1", 0});
    check_name_and_version(install_plan.install_actions[1], "b", {"1", 0});
}

TEST_CASE ("version install default features", "[versionplan]")
{
    MockVersionedPortfileProvider vp;

    auto a_x = make_fpgh("x");
    auto& a_scf = vp.emplace("a", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    a_scf->core_paragraph->default_features.emplace_back("x");
    a_scf->feature_paragraphs.push_back(std::move(a_x));

    MockCMakeVarProvider var_provider;

    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {Dependency{"a"}}, {}, toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 1);
    check_name_and_version(install_plan.install_actions[0], "a", {"1", 0}, {"x"});
}

TEST_CASE ("version dont install default features", "[versionplan]")
{
    MockVersionedPortfileProvider vp;

    auto a_x = make_fpgh("x");
    auto& a_scf = vp.emplace("a", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    a_scf->core_paragraph->default_features.emplace_back("x");
    a_scf->feature_paragraphs.push_back(std::move(a_x));

    MockCMakeVarProvider var_provider;

    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};

    auto install_plan =
        create_versioned_install_plan(vp, bp, var_provider, {Dependency{"a", {"core"}}}, {}, toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 1);
    check_name_and_version(install_plan.install_actions[0], "a", {"1", 0});
}

TEST_CASE ("version install transitive default features", "[versionplan]")
{
    MockVersionedPortfileProvider vp;

    auto a_x = make_fpgh("x");
    auto& a_scf = vp.emplace("a", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    a_scf->core_paragraph->default_features.emplace_back("x");
    a_scf->feature_paragraphs.push_back(std::move(a_x));

    auto& b_scf = vp.emplace("b", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    b_scf->core_paragraph->dependencies.push_back({"a", {"core"}});

    auto& c_scf = vp.emplace("c", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    c_scf->core_paragraph->dependencies.push_back({"a"});

    MockCMakeVarProvider var_provider;

    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};
    bp.v["c"] = {"1", 0};

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {Dependency{"b"}}, {}, toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 2);
    check_name_and_version(install_plan.install_actions[0], "a", {"1", 0}, {"x"});
    check_name_and_version(install_plan.install_actions[1], "b", {"1", 0});

    install_plan = create_versioned_install_plan(
                       vp, bp, var_provider, {Dependency{"a", {"core"}}, Dependency{"c"}}, {}, toplevel_spec())
                       .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 2);
    check_name_and_version(install_plan.install_actions[0], "a", {"1", 0}, {"x"});
    check_name_and_version(install_plan.install_actions[1], "c", {"1", 0});
}

static PlatformExpression::Expr parse_platform(StringView l)
{
    return PlatformExpression::parse_platform_expression(l, PlatformExpression::MultipleBinaryOperators::Deny)
        .value_or_exit(VCPKG_LINE_INFO);
}

TEST_CASE ("version install qualified dependencies", "[versionplan]")
{
    MockVersionedPortfileProvider vp;

    vp.emplace("b", {"1", 0}, VersionScheme::Relaxed);
    vp.emplace("c", {"1", 0}, VersionScheme::Relaxed);

    MockBaselineProvider bp;
    bp.v["b"] = {"1", 0};
    bp.v["c"] = {"1", 0};

    SECTION ("windows")
    {
        MockCMakeVarProvider var_provider;
        var_provider.dep_info_vars[toplevel_spec()] = {{"VCPKG_CMAKE_SYSTEM_NAME", "Windows"}};

        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          var_provider,
                                          {{"b", {}, parse_platform("!linux")}, {"c", {}, parse_platform("linux")}},
                                          {},
                                          toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 1);
        check_name_and_version(install_plan.install_actions[0], "b", {"1", 0});
    }

    SECTION ("linux")
    {
        MockCMakeVarProvider var_provider;
        var_provider.dep_info_vars[toplevel_spec()] = {{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}};

        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          var_provider,
                                          {{"b", {}, parse_platform("!linux")}, {"c", {}, parse_platform("linux")}},
                                          {},
                                          toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 1);
        check_name_and_version(install_plan.install_actions[0], "c", {"1", 0});
    }
}

TEST_CASE ("version install qualified default suppression", "[versionplan]")
{
    MockVersionedPortfileProvider vp;

    auto& a_scf = vp.emplace("a", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    a_scf->core_paragraph->default_features.emplace_back("x");
    a_scf->feature_paragraphs.push_back(make_fpgh("x"));

    vp.emplace("b", {"1", 0}, VersionScheme::Relaxed)
        .source_control_file->core_paragraph->dependencies.push_back({"a", {"core"}});

    MockCMakeVarProvider var_provider;

    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      var_provider,
                                      {{"b", {}, parse_platform("!linux")}, {"a", {"core"}, parse_platform("linux")}},
                                      {},
                                      toplevel_spec())
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 2);
    check_name_and_version(install_plan.install_actions[0], "a", {"1", 0}, {"x"});
    check_name_and_version(install_plan.install_actions[1], "b", {"1", 0});
}

TEST_CASE ("version install qualified transitive", "[versionplan]")
{
    MockVersionedPortfileProvider vp;

    vp.emplace("a", {"1", 0}, VersionScheme::Relaxed);
    vp.emplace("c", {"1", 0}, VersionScheme::Relaxed);

    auto& b_scf = vp.emplace("b", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    b_scf->core_paragraph->dependencies.push_back({"a", {}, parse_platform("!linux")});
    b_scf->core_paragraph->dependencies.push_back({"c", {}, parse_platform("linux")});

    MockCMakeVarProvider var_provider;

    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};
    bp.v["c"] = {"1", 0};

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {{"b"}}, {}, toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 2);
    check_name_and_version(install_plan.install_actions[0], "a", {"1", 0});
    check_name_and_version(install_plan.install_actions[1], "b", {"1", 0});
}

TEST_CASE ("version install different vars", "[versionplan]")
{
    MockVersionedPortfileProvider vp;

    auto& b_scf = vp.emplace("b", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    b_scf->core_paragraph->dependencies.push_back({"a", {}, parse_platform("!linux")});

    auto& a_scf = vp.emplace("a", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    a_scf->core_paragraph->dependencies.push_back({"c", {}, parse_platform("linux")});

    vp.emplace("c", {"1", 0}, VersionScheme::Relaxed);

    MockCMakeVarProvider var_provider;
    var_provider.dep_info_vars[PackageSpec{"a", Test::X86_WINDOWS}] = {{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}};

    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};
    bp.v["c"] = {"1", 0};

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {{"b"}}, {}, toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 3);
    check_name_and_version(install_plan.install_actions[0], "c", {"1", 0});
    check_name_and_version(install_plan.install_actions[1], "a", {"1", 0});
    check_name_and_version(install_plan.install_actions[2], "b", {"1", 0});
}

TEST_CASE ("version install qualified features", "[versionplan]")
{
    MockVersionedPortfileProvider vp;

    auto& b_scf = vp.emplace("b", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    b_scf->core_paragraph->default_features.emplace_back("x");
    b_scf->feature_paragraphs.push_back(make_fpgh("x"));
    b_scf->feature_paragraphs.back()->dependencies.push_back({"a", {}, parse_platform("!linux")});

    auto& a_scf = vp.emplace("a", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    a_scf->core_paragraph->default_features.emplace_back("y");
    a_scf->feature_paragraphs.push_back(make_fpgh("y"));
    a_scf->feature_paragraphs.back()->dependencies.push_back({"c", {}, parse_platform("linux")});

    auto& c_scf = vp.emplace("c", {"1", 0}, VersionScheme::Relaxed).source_control_file;
    c_scf->core_paragraph->default_features.emplace_back("z");
    c_scf->feature_paragraphs.push_back(make_fpgh("z"));
    c_scf->feature_paragraphs.back()->dependencies.push_back({"d", {}, parse_platform("linux")});

    vp.emplace("d", {"1", 0}, VersionScheme::Relaxed);

    MockCMakeVarProvider var_provider;
    var_provider.dep_info_vars[PackageSpec{"a", Test::X86_WINDOWS}] = {{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}};

    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};
    bp.v["c"] = {"1", 0};
    bp.v["d"] = {"1", 0};

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {{"b"}}, {}, toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 3);
    check_name_and_version(install_plan.install_actions[0], "c", {"1", 0}, {"z"});
    check_name_and_version(install_plan.install_actions[1], "a", {"1", 0}, {"y"});
    check_name_and_version(install_plan.install_actions[2], "b", {"1", 0}, {"x"});
}

TEST_CASE ("version install self features", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    auto& a_scf = vp.emplace("a", {"1", 0}).source_control_file;
    a_scf->feature_paragraphs.push_back(make_fpgh("x"));
    a_scf->feature_paragraphs.back()->dependencies.push_back({"a", {"core", "y"}});
    a_scf->feature_paragraphs.push_back(make_fpgh("y"));
    a_scf->feature_paragraphs.push_back(make_fpgh("z"));

    MockCMakeVarProvider var_provider;

    auto install_plan = create_versioned_install_plan(vp, bp, var_provider, {{"a", {"x"}}}, {}, toplevel_spec())
                            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 1);
    check_name_and_version(install_plan.install_actions[0], "a", {"1", 0}, {"x", "y"});
}

static auto create_versioned_install_plan(MockVersionedPortfileProvider& vp,
                                          MockBaselineProvider& bp,
                                          std::vector<Dependency> deps,
                                          MockCMakeVarProvider& var_provider)
{
    return create_versioned_install_plan(vp, bp, var_provider, deps, {}, toplevel_spec());
}

static auto create_versioned_install_plan(MockVersionedPortfileProvider& vp,
                                          MockBaselineProvider& bp,
                                          std::vector<Dependency> deps)
{
    MockCMakeVarProvider var_provider;
    return create_versioned_install_plan(vp, bp, deps, var_provider);
}

TEST_CASE ("version install nonexisting features", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    auto& a_scf = vp.emplace("a", {"1", 0}).source_control_file;
    a_scf->feature_paragraphs.push_back(make_fpgh("x"));

    auto install_plan = create_versioned_install_plan(vp, bp, {{"a", {"y"}}});

    REQUIRE_FALSE(install_plan.has_value());
}

TEST_CASE ("version install transitive missing features", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    auto& a_scf = vp.emplace("a", {"1", 0}).source_control_file;
    a_scf->core_paragraph->dependencies.push_back({"b", {"y"}});
    vp.emplace("b", {"1", 0});

    auto install_plan = create_versioned_install_plan(vp, bp, {{"a", {}}});

    REQUIRE_FALSE(install_plan.has_value());
}

TEST_CASE ("version remove features during upgrade", "[versionplan]")
{
    // This case tests the removal of a feature from a package (and corresponding removal of the requirement by other
    // dependents).

    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};
    bp.v["c"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    // a@0 -> b[x], c>=1
    auto& a_scf = vp.emplace("a", {"1", 0}).source_control_file;
    a_scf->core_paragraph->dependencies.push_back({"b", {"x"}});
    a_scf->core_paragraph->dependencies.push_back({"c", {}, {}, {VersionConstraintKind::Minimum, "1", 1}});
    // a@1 -> b
    auto& a1_scf = vp.emplace("a", {"1", 1}).source_control_file;
    a1_scf->core_paragraph->dependencies.push_back({"b"});
    // b@0 : [x]
    auto& b_scf = vp.emplace("b", {"1", 0}).source_control_file;
    b_scf->feature_paragraphs.push_back(make_fpgh("x"));
    // b@1 -> c
    auto& b1_scf = vp.emplace("b", {"1", 1}).source_control_file;
    b1_scf->core_paragraph->dependencies.push_back({"c"});
    vp.emplace("c", {"1", 0});
    vp.emplace("c", {"1", 1});

    auto install_plan =
        create_versioned_install_plan(vp,
                                      bp,
                                      {
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "1"}},
                                          Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "1", 1}},
                                          Dependency{"b", {}, {}, {VersionConstraintKind::Minimum, "1", 1}},
                                          Dependency{"c"},
                                      })
            .value_or_exit(VCPKG_LINE_INFO);

    REQUIRE(install_plan.size() == 3);
    check_name_and_version(install_plan.install_actions[0], "c", {"1", 1});
    check_name_and_version(install_plan.install_actions[1], "b", {"1", 1});
    check_name_and_version(install_plan.install_actions[2], "a", {"1", 1});
}

TEST_CASE ("version install host tool", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};
    bp.v["c"] = {"1", 0};
    bp.v["d"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0});
    auto& b_scf = vp.emplace("b", {"1", 0}).source_control_file;
    b_scf->core_paragraph->dependencies.push_back(Dependency{"a", {}, {}, {}, true});
    auto& c_scf = vp.emplace("c", {"1", 0}).source_control_file;
    c_scf->core_paragraph->dependencies.push_back(Dependency{"a"});
    auto& d_scf = vp.emplace("d", {"1", 0}).source_control_file;
    d_scf->core_paragraph->dependencies.push_back(Dependency{"d", {}, {}, {}, true});

    SECTION ("normal toplevel")
    {
        Dependency dep_c{"c"};

        auto install_plan = create_versioned_install_plan(vp, bp, {dep_c}).value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 2);
        check_name_and_version(install_plan.install_actions[0], "a", {"1", 0});
        REQUIRE(install_plan.install_actions[0].spec.triplet() == Test::X86_WINDOWS);
        check_name_and_version(install_plan.install_actions[1], "c", {"1", 0});
        REQUIRE(install_plan.install_actions[1].spec.triplet() == Test::X86_WINDOWS);
    }
    SECTION ("toplevel")
    {
        Dependency dep_a{"a"};
        dep_a.host = true;

        auto install_plan = create_versioned_install_plan(vp, bp, {dep_a}).value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 1);
        check_name_and_version(install_plan.install_actions[0], "a", {"1", 0});
        REQUIRE(install_plan.install_actions[0].spec.triplet() == Test::ARM_UWP);
    }
    SECTION ("transitive 1")
    {
        auto install_plan = create_versioned_install_plan(vp, bp, {{"b"}}).value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 2);
        check_name_and_version(install_plan.install_actions[0], "a", {"1", 0});
        REQUIRE(install_plan.install_actions[0].spec.triplet() == Test::ARM_UWP);
        CHECK(install_plan.install_actions[0].request_type == RequestType::AUTO_SELECTED);
        check_name_and_version(install_plan.install_actions[1], "b", {"1", 0});
        REQUIRE(install_plan.install_actions[1].spec.triplet() == Test::X86_WINDOWS);
        CHECK(install_plan.install_actions[1].request_type == RequestType::USER_REQUESTED);
    }
    SECTION ("transitive 2")
    {
        Dependency dep_c{"c"};
        dep_c.host = true;

        auto install_plan = create_versioned_install_plan(vp, bp, {dep_c}).value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 2);
        check_name_and_version(install_plan.install_actions[0], "a", {"1", 0});
        REQUIRE(install_plan.install_actions[0].spec.triplet() == Test::ARM_UWP);
        CHECK(install_plan.install_actions[0].request_type == RequestType::AUTO_SELECTED);
        check_name_and_version(install_plan.install_actions[1], "c", {"1", 0});
        REQUIRE(install_plan.install_actions[1].spec.triplet() == Test::ARM_UWP);
        CHECK(install_plan.install_actions[1].request_type == RequestType::USER_REQUESTED);
    }
    SECTION ("self-reference")
    {
        auto install_plan = create_versioned_install_plan(vp, bp, {{"d"}}).value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 2);
        check_name_and_version(install_plan.install_actions[0], "d", {"1", 0});
        REQUIRE(install_plan.install_actions[0].spec.triplet() == Test::ARM_UWP);
        CHECK(install_plan.install_actions[0].request_type == RequestType::AUTO_SELECTED);
        check_name_and_version(install_plan.install_actions[1], "d", {"1", 0});
        REQUIRE(install_plan.install_actions[1].spec.triplet() == Test::X86_WINDOWS);
        CHECK(install_plan.install_actions[1].request_type == RequestType::USER_REQUESTED);
    }
}
TEST_CASE ("version overlay ports", "[versionplan]")
{
    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};
    bp.v["b"] = {"1", 0};
    bp.v["c"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0});
    vp.emplace("a", {"1", 1});
    vp.emplace("a", {"2", 0});
    vp.emplace("b", {"1", 0}).source_control_file->core_paragraph->dependencies.emplace_back(Dependency{"a"});
    vp.emplace("c", {"1", 0})
        .source_control_file->core_paragraph->dependencies.emplace_back(
            Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "1", 1}});

    MockCMakeVarProvider var_provider;

    MockOverlayProvider oprovider;
    oprovider.emplace("a", {"overlay", 0});

    SECTION ("no baseline")
    {
        const MockBaselineProvider empty_bp;

        auto install_plan =
            create_versioned_install_plan(vp, empty_bp, oprovider, var_provider, {{"a"}}, {}, toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 1);
        check_name_and_version(install_plan.install_actions[0], "a", {"overlay", 0});
    }

    SECTION ("transitive")
    {
        auto install_plan = create_versioned_install_plan(vp, bp, oprovider, var_provider, {{"b"}}, {}, toplevel_spec())
                                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 2);
        check_name_and_version(install_plan.install_actions[0], "a", {"overlay", 0});
        check_name_and_version(install_plan.install_actions[1], "b", {"1", 0});
    }

    SECTION ("transitive constraint")
    {
        auto install_plan = create_versioned_install_plan(vp, bp, oprovider, var_provider, {{"c"}}, {}, toplevel_spec())
                                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 2);
        check_name_and_version(install_plan.install_actions[0], "a", {"overlay", 0});
        check_name_and_version(install_plan.install_actions[1], "c", {"1", 0});
    }

    SECTION ("none")
    {
        auto install_plan = create_versioned_install_plan(vp, bp, oprovider, var_provider, {{"a"}}, {}, toplevel_spec())
                                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 1);
        check_name_and_version(install_plan.install_actions[0], "a", {"overlay", 0});
    }
    SECTION ("constraint")
    {
        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          oprovider,
                                          var_provider,
                                          {
                                              Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "1", 1}},
                                          },
                                          {},
                                          toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 1);
        check_name_and_version(install_plan.install_actions[0], "a", {"overlay", 0});
    }
    SECTION ("constraint+override")
    {
        auto install_plan =
            create_versioned_install_plan(vp,
                                          bp,
                                          oprovider,
                                          var_provider,
                                          {
                                              Dependency{"a", {}, {}, {VersionConstraintKind::Minimum, "1", 1}},
                                          },
                                          {
                                              DependencyOverride{"a", "2", 0},
                                          },
                                          toplevel_spec())
                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 1);
        check_name_and_version(install_plan.install_actions[0], "a", {"overlay", 0});
    }
    SECTION ("override")
    {
        auto install_plan = create_versioned_install_plan(vp,
                                                          bp,
                                                          oprovider,
                                                          var_provider,
                                                          {
                                                              Dependency{"a"},
                                                          },
                                                          {
                                                              DependencyOverride{"a", "2", 0},
                                                          },
                                                          toplevel_spec())
                                .value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(install_plan.size() == 1);
        check_name_and_version(install_plan.install_actions[0], "a", {"overlay", 0});
    }
}

TEST_CASE ("respect supports expression", "[versionplan]")
{
    using namespace PlatformExpression;
    const auto supports_expression =
        parse_platform_expression("windows", MultipleBinaryOperators::Deny).value_or_exit(VCPKG_LINE_INFO);
    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    vp.emplace("a", {"1", 0}).source_control_file->core_paragraph->supports_expression = supports_expression;
    vp.emplace("a", {"1", 1});
    MockCMakeVarProvider var_provider;
    var_provider.dep_info_vars[{"a", toplevel_spec().triplet()}]["VCPKG_CMAKE_SYSTEM_NAME"] = "";
    auto install_plan = create_versioned_install_plan(vp, bp, {{"a", {}}}, var_provider);
    CHECK(install_plan.has_value());
    var_provider.dep_info_vars[{"a", toplevel_spec().triplet()}]["VCPKG_CMAKE_SYSTEM_NAME"] = "Linux";
    install_plan = create_versioned_install_plan(vp, bp, {{"a", {}}}, var_provider);
    CHECK_FALSE(install_plan.has_value());
    SECTION ("override")
    {
        // override from non supported to supported version
        MockOverlayProvider oprovider;
        install_plan = create_versioned_install_plan(
            vp, bp, oprovider, var_provider, {Dependency{"a"}}, {DependencyOverride{"a", "1", 1}}, toplevel_spec());
        CHECK(install_plan.has_value());
        // override from supported to non supported version
        bp.v["a"] = {"1", 1};
        install_plan = create_versioned_install_plan(
            vp, bp, oprovider, var_provider, {Dependency{"a"}}, {DependencyOverride{"a", "1", 0}}, toplevel_spec());
        CHECK_FALSE(install_plan.has_value());
    }
}

TEST_CASE ("respect supports expressions of features", "[versionplan]")
{
    using namespace PlatformExpression;
    MockBaselineProvider bp;
    bp.v["a"] = {"1", 0};

    MockVersionedPortfileProvider vp;
    auto a_x = std::make_unique<FeatureParagraph>();
    a_x->name = "x";
    a_x->supports_expression =
        parse_platform_expression("windows", MultipleBinaryOperators::Deny).value_or_exit(VCPKG_LINE_INFO);
    vp.emplace("a", {"1", 0}).source_control_file->feature_paragraphs.push_back(std::move(a_x));
    a_x = std::make_unique<FeatureParagraph>();
    a_x->name = "x";
    vp.emplace("a", {"1", 1}).source_control_file->feature_paragraphs.push_back(std::move(a_x));

    MockCMakeVarProvider var_provider;
    var_provider.dep_info_vars[{"a", toplevel_spec().triplet()}]["VCPKG_CMAKE_SYSTEM_NAME"] = "";
    auto install_plan = create_versioned_install_plan(vp, bp, {{"a", {"x"}}}, var_provider);
    CHECK(install_plan.has_value());
    var_provider.dep_info_vars[{"a", toplevel_spec().triplet()}]["VCPKG_CMAKE_SYSTEM_NAME"] = "Linux";
    install_plan = create_versioned_install_plan(vp, bp, {{"a", {"x"}}}, var_provider);
    CHECK_FALSE(install_plan.has_value());
    SECTION ("override")
    {
        // override from non supported to supported version
        MockOverlayProvider oprovider;
        install_plan = create_versioned_install_plan(vp,
                                                     bp,
                                                     oprovider,
                                                     var_provider,
                                                     {Dependency{"a", {"x"}}},
                                                     {DependencyOverride{"a", "1", 1}},
                                                     toplevel_spec());
        CHECK(install_plan.has_value());
        // override from supported to non supported version
        bp.v["a"] = {"1", 1};
        install_plan = create_versioned_install_plan(vp,
                                                     bp,
                                                     oprovider,
                                                     var_provider,
                                                     {Dependency{"a", {"x"}}},
                                                     {DependencyOverride{"a", "1", 0}},
                                                     toplevel_spec());
        CHECK_FALSE(install_plan.has_value());
    }
}
