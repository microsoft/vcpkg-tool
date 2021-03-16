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

/// <summary>
/// Assert that the given action an install of given features from given package.
/// </summary>
static void features_check(Dependencies::InstallPlanAction& plan,
                           std::string pkg_name,
                           std::vector<std::string> expected_features,
                           Triplet triplet = Test::X86_WINDOWS)
{
    const auto& feature_list = plan.feature_list;

    REQUIRE(plan.spec.triplet().to_string() == triplet.to_string());
    REQUIRE(pkg_name == plan.spec.name());
    REQUIRE(feature_list.size() == expected_features.size());

    for (auto&& feature_name : expected_features)
    {
        // TODO: see if this can be simplified
        if (feature_name == "core" || feature_name.empty())
        {
            REQUIRE((Util::find(feature_list, "core") != feature_list.end() ||
                     Util::find(feature_list, "") != feature_list.end()));
            continue;
        }
        REQUIRE(Util::find(feature_list, feature_name) != feature_list.end());
    }
}

/// <summary>
/// Assert that the given action is a remove of given package.
/// </summary>
static void remove_plan_check(Dependencies::RemovePlanAction& plan,
                              std::string pkg_name,
                              Triplet triplet = Test::X86_WINDOWS)
{
    REQUIRE(plan.spec.triplet().to_string() == triplet.to_string());
    REQUIRE(pkg_name == plan.spec.name());
}

TEST_CASE ("basic install scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a", "b");
    auto spec_b = spec_map.emplace("b", "c");
    auto spec_c = spec_map.emplace("c");

    PortFileProvider::MapPortFileProvider map_port(spec_map.map);
    MockCMakeVarProvider var_provider;

    auto fullspec_a = FullPackageSpec{spec_a, {}};
    auto install_plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, {&fullspec_a, 1}, StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 3);
    REQUIRE(install_plan.install_actions.at(0).spec.name() == "c");
    REQUIRE(install_plan.install_actions.at(1).spec.name() == "b");
    REQUIRE(install_plan.install_actions.at(2).spec.name() == "a");
}

TEST_CASE ("multiple install scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a", "d");
    auto spec_b = spec_map.emplace("b", "d, e");
    auto spec_c = spec_map.emplace("c", "e, h");
    auto spec_d = spec_map.emplace("d", "f, g, h");
    auto spec_e = spec_map.emplace("e", "g");
    auto spec_f = spec_map.emplace("f");
    auto spec_g = spec_map.emplace("g");
    auto spec_h = spec_map.emplace("h");

    PortFileProvider::MapPortFileProvider map_port(spec_map.map);
    MockCMakeVarProvider var_provider;

    std::vector<FullPackageSpec> full_package_specs{
        FullPackageSpec{spec_a}, FullPackageSpec{spec_b}, FullPackageSpec{spec_c}};
    auto install_plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, full_package_specs, StatusParagraphs(std::move(status_paragraphs)));

    auto iterator_pos = [&](const PackageSpec& spec) {
        auto it = std::find_if(install_plan.install_actions.begin(),
                               install_plan.install_actions.end(),
                               [&](auto& action) { return action.spec == spec; });
        REQUIRE(it != install_plan.install_actions.end());
        return it - install_plan.install_actions.begin();
    };

    const auto a_pos = iterator_pos(spec_a);
    const auto b_pos = iterator_pos(spec_b);
    const auto c_pos = iterator_pos(spec_c);
    const auto d_pos = iterator_pos(spec_d);
    const auto e_pos = iterator_pos(spec_e);
    const auto f_pos = iterator_pos(spec_f);
    const auto g_pos = iterator_pos(spec_g);
    const auto h_pos = iterator_pos(spec_h);

    REQUIRE(a_pos > d_pos);
    REQUIRE(b_pos > e_pos);
    REQUIRE(b_pos > d_pos);
    REQUIRE(c_pos > e_pos);
    REQUIRE(c_pos > h_pos);
    REQUIRE(d_pos > f_pos);
    REQUIRE(d_pos > g_pos);
    REQUIRE(d_pos > h_pos);
    REQUIRE(e_pos > g_pos);
}

TEST_CASE ("existing package scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(vcpkg::Test::make_status_pgh("a"));

    PackageSpecMap spec_map;
    auto spec_a = FullPackageSpec{spec_map.emplace("a")};

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, {&spec_a, 1}, StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 1);
    const auto p = &install_plan.already_installed.at(0);
    REQUIRE(p->spec.name() == "a");
    REQUIRE(p->plan_type == Dependencies::InstallPlanType::ALREADY_INSTALLED);
    REQUIRE(p->request_type == Dependencies::RequestType::USER_REQUESTED);
}

TEST_CASE ("user requested package scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    PackageSpecMap spec_map;
    const auto spec_a = FullPackageSpec{spec_map.emplace("a", "b")};
    const auto spec_b = FullPackageSpec{spec_map.emplace("b")};

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    const auto install_plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, {&spec_a, 1}, StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 2);
    const auto p = &install_plan.install_actions.at(0);
    REQUIRE(p->spec.name() == "b");
    REQUIRE(p->plan_type == Dependencies::InstallPlanType::BUILD_AND_INSTALL);
    REQUIRE(p->request_type == Dependencies::RequestType::AUTO_SELECTED);

    const auto p2 = &install_plan.install_actions.at(1);
    REQUIRE(p2->spec.name() == "a");
    REQUIRE(p2->plan_type == Dependencies::InstallPlanType::BUILD_AND_INSTALL);
    REQUIRE(p2->request_type == Dependencies::RequestType::USER_REQUESTED);
}

TEST_CASE ("long install scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("j", "k"));
    status_paragraphs.push_back(make_status_pgh("k"));

    PackageSpecMap spec_map;

    auto spec_a = spec_map.emplace("a", "b, c, d, e, f, g, h, j, k");
    auto spec_b = spec_map.emplace("b", "c, d, e, f, g, h, j, k");
    auto spec_c = spec_map.emplace("c", "d, e, f, g, h, j, k");
    auto spec_d = spec_map.emplace("d", "e, f, g, h, j, k");
    auto spec_e = spec_map.emplace("e", "f, g, h, j, k");
    auto spec_f = spec_map.emplace("f", "g, h, j, k");
    auto spec_g = spec_map.emplace("g", "h, j, k");
    auto spec_h = spec_map.emplace("h", "j, k");
    auto spec_j = spec_map.emplace("j", "k");
    auto spec_k = spec_map.emplace("k");

    PortFileProvider::MapPortFileProvider map_port(spec_map.map);
    MockCMakeVarProvider var_provider;

    auto fullspec_a = FullPackageSpec{spec_a};
    auto plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, {&fullspec_a, 1}, StatusParagraphs(std::move(status_paragraphs)));

    auto& install_plan = plan.install_actions;
    REQUIRE(install_plan.size() == 8);
    REQUIRE(install_plan.at(0).spec.name() == "h");
    REQUIRE(install_plan.at(1).spec.name() == "g");
    REQUIRE(install_plan.at(2).spec.name() == "f");
    REQUIRE(install_plan.at(3).spec.name() == "e");
    REQUIRE(install_plan.at(4).spec.name() == "d");
    REQUIRE(install_plan.at(5).spec.name() == "c");
    REQUIRE(install_plan.at(6).spec.name() == "b");
    REQUIRE(install_plan.at(7).spec.name() == "a");
}

TEST_CASE ("basic feature test 1", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("a", "b, b[b1]"));
    status_paragraphs.push_back(make_status_pgh("b"));
    status_paragraphs.push_back(make_status_feature_pgh("b", "b1"));

    PackageSpecMap spec_map;
    auto spec_a = FullPackageSpec{spec_map.emplace("a", "b, b[b1]", {{"a1", "b[b2]"}}), {"a1"}};
    auto spec_b = FullPackageSpec{spec_map.emplace("b", "", {{"b1", ""}, {"b2", ""}, {"b3", ""}})};

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, {&spec_a, 1}, StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(plan.size() == 4);
    remove_plan_check(plan.remove_actions.at(0), "a");
    remove_plan_check(plan.remove_actions.at(1), "b");
    features_check(plan.install_actions.at(0), "b", {"b1", "core", "b1"});
    features_check(plan.install_actions.at(1), "a", {"a1", "core"});
}

TEST_CASE ("basic feature test 2", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    PackageSpecMap spec_map;

    auto spec_a = FullPackageSpec{spec_map.emplace("a", "b[b1]", {{"a1", "b[b2]"}}), {"a1"}};
    auto spec_b = FullPackageSpec{spec_map.emplace("b", "", {{"b1", ""}, {"b2", ""}, {"b3", ""}})};

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, {&spec_a, 1}, StatusParagraphs(std::move(status_paragraphs)));

    auto& install_plan = plan.install_actions;
    REQUIRE(install_plan.size() == 2);
    features_check(install_plan.at(0), "b", {"b1", "b2", "core"});
    features_check(install_plan.at(1), "a", {"a1", "core"});
}

TEST_CASE ("basic feature test 3", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("a"));

    PackageSpecMap spec_map;

    auto spec_a = FullPackageSpec{spec_map.emplace("a", "b", {{"a1", ""}}), {"core"}};
    auto spec_b = FullPackageSpec{spec_map.emplace("b")};
    auto spec_c = FullPackageSpec{spec_map.emplace("c", "a[a1]"), {"core"}};

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    std::vector<FullPackageSpec> full_package_specs{spec_c, spec_a};
    auto plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, full_package_specs, StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(plan.size() == 4);
    remove_plan_check(plan.remove_actions.at(0), "a");
    auto& install_plan = plan.install_actions;
    features_check(install_plan.at(0), "b", {"core"});
    features_check(install_plan.at(1), "a", {"a1", "core"});
    features_check(install_plan.at(2), "c", {"core"});
}

TEST_CASE ("basic feature test 4", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("a"));
    status_paragraphs.push_back(make_status_feature_pgh("a", "a1", ""));

    PackageSpecMap spec_map;

    auto spec_a = FullPackageSpec{spec_map.emplace("a", "b", {{"a1", ""}})};
    auto spec_b = FullPackageSpec{spec_map.emplace("b")};
    auto spec_c = FullPackageSpec{spec_map.emplace("c", "a[a1]"), {"core"}};

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, {&spec_c, 1}, StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 1);
    features_check(install_plan.install_actions.at(0), "c", {"core"});
}

TEST_CASE ("basic feature test 5", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    PackageSpecMap spec_map;

    auto spec_a =
        FullPackageSpec{spec_map.emplace("a", "", {{"a1", "b[b1]"}, {"a2", "b[b2]"}, {"a3", "a[a2]"}}), {"a3"}};
    auto spec_b = FullPackageSpec{spec_map.emplace("b", "", {{"b1", ""}, {"b2", ""}})};

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, {&spec_a, 1}, StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 2);
    features_check(install_plan.install_actions.at(0), "b", {"core", "b2"});
    features_check(install_plan.install_actions.at(1), "a", {"core", "a3", "a2"});
}

TEST_CASE ("basic feature test 6", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("b"));

    PackageSpecMap spec_map;
    auto spec_a = FullPackageSpec{spec_map.emplace("a", "b[core]"), {"core"}};
    auto spec_b = FullPackageSpec{spec_map.emplace("b", "", {{"b1", ""}}), {"b1"}};

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    std::vector<FullPackageSpec> full_package_specs{spec_a, spec_b};
    auto plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, full_package_specs, StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(plan.size() == 3);
    remove_plan_check(plan.remove_actions.at(0), "b");
    features_check(plan.install_actions.at(0), "b", {"core", "b1"});
    features_check(plan.install_actions.at(1), "a", {"core"});
}

TEST_CASE ("basic feature test 7", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("x", "b"));
    status_paragraphs.push_back(make_status_pgh("b"));

    PackageSpecMap spec_map;

    auto spec_a = FullPackageSpec{spec_map.emplace("a")};
    auto spec_x = FullPackageSpec{spec_map.emplace("x", "a"), {"core"}};
    auto spec_b = FullPackageSpec{spec_map.emplace("b", "", {{"b1", ""}}), {"b1"}};

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, {&spec_b, 1}, StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(plan.size() == 5);
    remove_plan_check(plan.remove_actions.at(0), "x");
    remove_plan_check(plan.remove_actions.at(1), "b");

    features_check(plan.install_actions.at(0), "a", {"core"});
    features_check(plan.install_actions.at(1), "b", {"core", "b1"});
    features_check(plan.install_actions.at(2), "x", {"core"});
}

TEST_CASE ("basic feature test 8", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("a"));
    status_paragraphs.push_back(make_status_pgh("a"));
    status_paragraphs.back()->package.spec = PackageSpec("a", Test::X64_WINDOWS);

    PackageSpecMap spec_map(Test::X64_WINDOWS);
    auto spec_a_64 = FullPackageSpec{spec_map.emplace("a", "b", {{"a1", ""}}), {"core"}};
    auto spec_b_64 = FullPackageSpec{spec_map.emplace("b")};
    auto spec_c_64 = FullPackageSpec{spec_map.emplace("c", "a[a1]"), {"core"}};

    spec_map.triplet = Test::X86_WINDOWS;
    auto spec_a_86 = FullPackageSpec{PackageSpec{"a", Test::X86_WINDOWS}};
    auto spec_b_86 = FullPackageSpec{PackageSpec{"b", Test::X86_WINDOWS}};
    auto spec_c_86 = FullPackageSpec{PackageSpec{"c", Test::X86_WINDOWS}};

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    std::vector<FullPackageSpec> full_package_specs{spec_c_64, spec_a_86, spec_a_64, spec_c_86};
    auto plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, full_package_specs, StatusParagraphs(std::move(status_paragraphs)));

    remove_plan_check(plan.remove_actions.at(0), "a", Test::X64_WINDOWS);
    remove_plan_check(plan.remove_actions.at(1), "a");
    auto& install_plan = plan.install_actions;
    features_check(install_plan.at(0), "b", {"core"}, Test::X64_WINDOWS);
    features_check(install_plan.at(1), "a", {"a1", "core"}, Test::X64_WINDOWS);
    features_check(install_plan.at(2), "b", {"core"});
    features_check(install_plan.at(3), "a", {"a1", "core"});
    features_check(install_plan.at(4), "c", {"core"}, Test::X64_WINDOWS);
    features_check(install_plan.at(5), "c", {"core"});
}

TEST_CASE ("install all features test", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    PackageSpecMap spec_map(Test::X64_WINDOWS);
    auto spec_a_64 = FullPackageSpec{spec_map.emplace("a", "", {{"0", ""}, {"1", ""}}), {"core"}};

    auto install_specs = FullPackageSpec::from_string("a[*]", Test::X64_WINDOWS);
    REQUIRE(install_specs.has_value());
    if (!install_specs.has_value()) return;

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 1);
    features_check(install_plan.install_actions.at(0), "a", {"0", "1", "core"}, Test::X64_WINDOWS);
}

TEST_CASE ("install default features test 1", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    // Add a port "a" with default features "1" and features "0" and "1".
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    spec_map.emplace("a", "", {{"0", ""}, {"1", ""}}, {"1"});

    // Install "a" (without explicit feature specification)
    auto install_specs = FullPackageSpec::from_string("a", Test::X64_WINDOWS);

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    // Expect the default feature "1" to be installed, but not "0"
    REQUIRE(install_plan.size() == 1);
    features_check(install_plan.install_actions.at(0), "a", {"1", "core"}, Test::X64_WINDOWS);
}

TEST_CASE ("install default features test 2", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("a"));
    status_paragraphs.back()->package.spec = PackageSpec("a", Test::X64_WINDOWS);

    // Add a port "a" of which "core" is already installed, but we will
    // install the default features "explicitly"
    // "a" has two features, of which "a1" is default.
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    spec_map.emplace("a", "", {{"a0", ""}, {"a1", ""}}, {"a1"});

    // Install "a" (without explicit feature specification)
    auto install_specs = FullPackageSpec::from_string("a", Test::X64_WINDOWS);

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    // Expect "a" to get removed for rebuild and then installed with default
    // features.
    REQUIRE(install_plan.size() == 2);
    remove_plan_check(install_plan.remove_actions.at(0), "a", Test::X64_WINDOWS);
    features_check(install_plan.install_actions.at(0), "a", {"a1", "core"}, Test::X64_WINDOWS);
}

TEST_CASE ("install default features test 3", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    // "a" has two features, of which "a1" is default.
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    spec_map.emplace("a", "", {{"a0", ""}, {"a1", ""}}, {"a1"});

    // Explicitly install "a" without default features
    auto install_specs = FullPackageSpec::from_string("a[core]", Test::X64_WINDOWS);

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    // Expect the default feature not to get installed.
    REQUIRE(install_plan.size() == 1);
    features_check(install_plan.install_actions.at(0), "a", {"core"}, Test::X64_WINDOWS);
}

TEST_CASE ("install default features of dependency test 1", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    // Add a port "a" which depends on the core of "b"
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    spec_map.emplace("a", "b[core]");
    // "b" has two features, of which "b1" is default.
    spec_map.emplace("b", "", {{"b0", ""}, {"b1", ""}}, {"b1"});

    // Install "a" (without explicit feature specification)
    auto install_specs = FullPackageSpec::from_string("a", Test::X64_WINDOWS);
    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    // Expect "a" to get installed and defaults of "b" through the dependency,
    // as no explicit features of "b" are installed by the user.
    REQUIRE(install_plan.size() == 2);
    features_check(install_plan.install_actions.at(0), "b", {"b1", "core"}, Test::X64_WINDOWS);
    features_check(install_plan.install_actions.at(1), "a", {"core"}, Test::X64_WINDOWS);
}

TEST_CASE ("do not install default features of dependency test 1", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    // Add a port "a" which depends on the core of "b"
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    spec_map.emplace("a", "b[core]");
    // "b" has two features, of which "b1" is default.
    spec_map.emplace("b", "", {{"b0", ""}, {"b1", ""}}, {"b1"});

    // Install "a" (without explicit feature specification)
    auto spec_a = FullPackageSpec::from_string("a", Test::X64_WINDOWS);
    auto spec_b = FullPackageSpec::from_string("b[core]", Test::X64_WINDOWS);
    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    std::vector<FullPackageSpec> full_package_specs;
    full_package_specs.push_back(spec_a.value_or_exit(VCPKG_LINE_INFO));
    full_package_specs.push_back(spec_b.value_or_exit(VCPKG_LINE_INFO));
    auto install_plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, full_package_specs, StatusParagraphs(std::move(status_paragraphs)));

    // Expect "a" to get installed and defaults of "b" through the dependency,
    // as no explicit features of "b" are installed by the user.
    REQUIRE(install_plan.size() == 2);
    features_check(install_plan.install_actions.at(0), "b", {"core"}, Test::X64_WINDOWS);
    features_check(install_plan.install_actions.at(1), "a", {"core"}, Test::X64_WINDOWS);
}

TEST_CASE ("install default features of dependency test 2", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    // Add a port "a" which depends on the default features of "b"
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    spec_map.emplace("a", "b");
    // "b" has two features, of which "b1" is default.
    spec_map.emplace("b", "", {{"b0", ""}, {"b1", ""}}, {"b1"});

    // Install "a" (without explicit feature specification)
    auto spec_a = FullPackageSpec::from_string("a", Test::X64_WINDOWS);
    auto spec_b = FullPackageSpec::from_string("b[core]", Test::X64_WINDOWS);
    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    std::vector<FullPackageSpec> full_package_specs;
    full_package_specs.push_back(spec_a.value_or_exit(VCPKG_LINE_INFO));
    full_package_specs.push_back(spec_b.value_or_exit(VCPKG_LINE_INFO));
    auto install_plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, full_package_specs, StatusParagraphs(std::move(status_paragraphs)));

    // Expect "a" to get installed and defaults of "b" through the dependency
    REQUIRE(install_plan.size() == 2);
    features_check(install_plan.install_actions.at(0), "b", {"b1", "core"}, Test::X64_WINDOWS);
    features_check(install_plan.install_actions.at(1), "a", {"core"}, Test::X64_WINDOWS);
}

TEST_CASE ("do not install default features of existing dependency", "[plan]")
{
    // Add a port "a" which depends on the core of "b"
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    spec_map.emplace("a", "b[core]");
    // "b" has two features, of which "b1" is default.
    spec_map.emplace("b", "", {{"b0", ""}, {"b1", ""}}, {"b1"});

    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    // "b[core]" is already installed
    status_paragraphs.push_back(make_status_pgh("b"));
    status_paragraphs.back()->package.spec = PackageSpec("b", Test::X64_WINDOWS);

    // Install "a" (without explicit feature specification)
    auto install_specs = FullPackageSpec::from_string("a", Test::X64_WINDOWS);
    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    // Expect "a" to get installed, but not require rebuilding "b"
    REQUIRE(install_plan.size() == 1);
    features_check(install_plan.install_actions.at(0), "a", {"core"}, Test::X64_WINDOWS);
}

TEST_CASE ("install default features of existing dependency", "[plan]")
{
    // Add a port "a" which depends on the default features of "b"
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    spec_map.emplace("a", "b");
    // "b" has a default feature
    spec_map.emplace("b", "", {{"b1", ""}}, {"b1"});

    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    // "b[core]" is already installed
    status_paragraphs.push_back(make_status_pgh("b", "", "b1"));
    status_paragraphs.back()->package.spec = PackageSpec("b", Test::X64_WINDOWS);

    // Install "a" (without explicit feature specification)
    auto install_specs = FullPackageSpec::from_string("a", Test::X64_WINDOWS);
    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    // Expect "b" to be rebuilt
    REQUIRE(install_plan.install_actions.size() == 2);
    features_check(install_plan.install_actions.at(0), "b", {"core", "b1"}, Test::X64_WINDOWS);
}

TEST_CASE ("install default features of dependency test 3", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;
    status_paragraphs.push_back(make_status_pgh("b"));
    status_paragraphs.back()->package.spec = PackageSpec("b", Test::X64_WINDOWS);

    // Add a port "a" which depends on the core of "b", which was already
    // installed explicitly
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    spec_map.emplace("a", "b[core]");
    // "b" has two features, of which "b1" is default.
    spec_map.emplace("b", "", {{"b0", ""}, {"b1", ""}}, {"b1"});

    // Install "a" (without explicit feature specification)
    auto install_specs = FullPackageSpec::from_string("a", Test::X64_WINDOWS);
    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    // Expect "a" to get installed, not the defaults of "b", as the required
    // dependencies are already there, installed explicitly by the user.
    REQUIRE(install_plan.size() == 1);
    features_check(install_plan.install_actions.at(0), "a", {"core"}, Test::X64_WINDOWS);
}

TEST_CASE ("install plan action dependencies", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    // Add a port "a" which depends on the core of "b", which was already
    // installed explicitly
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    auto spec_c = spec_map.emplace("c");
    auto spec_b = spec_map.emplace("b", "c");
    spec_map.emplace("a", "b");

    // Install "a" (without explicit feature specification)
    auto install_specs = FullPackageSpec::from_string("a", Test::X64_WINDOWS);
    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 3);
    features_check(install_plan.install_actions.at(0), "c", {"core"}, Test::X64_WINDOWS);

    // TODO: Figure out what to do with these tests
    features_check(install_plan.install_actions.at(1), "b", {"core"}, Test::X64_WINDOWS);
    // REQUIRE(install_plan.at(1).install_action.get()->computed_dependencies == std::vector<PackageSpec>{spec_c});

    features_check(install_plan.install_actions.at(2), "a", {"core"}, Test::X64_WINDOWS);
    // REQUIRE(install_plan.at(2).install_action.get()->computed_dependencies == std::vector<PackageSpec>{spec_b});
}

TEST_CASE ("install plan action dependencies 2", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    // Add a port "a" which depends on the core of "b", which was already
    // installed explicitly
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    auto spec_c = spec_map.emplace("c");
    auto spec_b = spec_map.emplace("b", "c");
    spec_map.emplace("a", "c, b");

    // Install "a" (without explicit feature specification)
    auto install_specs = FullPackageSpec::from_string("a", Test::X64_WINDOWS);
    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 3);
    features_check(install_plan.install_actions.at(0), "c", {"core"}, Test::X64_WINDOWS);

    features_check(install_plan.install_actions.at(1), "b", {"core"}, Test::X64_WINDOWS);
    // REQUIRE(install_plan.at(1).install_action.get()->computed_dependencies == std::vector<PackageSpec>{spec_c});

    features_check(install_plan.install_actions.at(2), "a", {"core"}, Test::X64_WINDOWS);
    // REQUIRE(install_plan.at(2).install_action.get()->computed_dependencies == std::vector<PackageSpec>{spec_b,
    // spec_c});
}

TEST_CASE ("install plan action dependencies 3", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    // Add a port "a" which depends on the core of "b", which was already
    // installed explicitly
    PackageSpecMap spec_map(Test::X64_WINDOWS);
    spec_map.emplace("a", "", {{"0", ""}, {"1", "a[0]"}}, {"1"});

    // Install "a" (without explicit feature specification)
    auto install_specs = FullPackageSpec::from_string("a", Test::X64_WINDOWS);
    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 1);
    features_check(install_plan.install_actions.at(0), "a", {"1", "0", "core"}, Test::X64_WINDOWS);
    // REQUIRE(install_plan.at(0).install_action.get()->computed_dependencies == std::vector<PackageSpec>{});
}

TEST_CASE ("install with default features", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a", ""));
    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto b_spec = spec_map.emplace("b", "", {{"0", ""}}, {"0"});
    auto a_spec = spec_map.emplace("a", "b[core]", {{"0", ""}});

    PortFileProvider::MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;

    std::vector<FullPackageSpec> full_package_specs{
        FullPackageSpec{a_spec, {"0"}},
        FullPackageSpec{b_spec, {"core"}},
    };

    auto install_plan = Dependencies::create_feature_install_plan(
        map_port, var_provider, full_package_specs, StatusParagraphs(std::move(status_db)));

    // Install "a" and indicate that "b" should not install default features
    REQUIRE(install_plan.size() == 3);
    remove_plan_check(install_plan.remove_actions.at(0), "a");
    features_check(install_plan.install_actions.at(0), "b", {"core"});
    features_check(install_plan.install_actions.at(1), "a", {"0", "core"});
}

TEST_CASE ("upgrade with default features 1", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a", "", "1"));
    pghs.push_back(make_status_feature_pgh("a", "0"));
    StatusParagraphs status_db(std::move(pghs));

    // Add a port "a" of which "core" and "0" are already installed.
    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a", "", {{"0", ""}, {"1", ""}}, {"1"});

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto plan = Dependencies::create_upgrade_plan(provider, var_provider, {spec_a}, status_db);

    // The upgrade should not install the default feature
    REQUIRE(plan.size() == 2);

    remove_plan_check(plan.remove_actions.at(0), "a");
    features_check(plan.install_actions.at(0), "a", {"core", "0"});
}

TEST_CASE ("upgrade with default features 2", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    // B is currently installed _without_ default feature b0
    pghs.push_back(make_status_pgh("b", "", "b0", "x64-windows"));
    pghs.push_back(make_status_pgh("a", "b[core]", "", "x64-windows"));

    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map(Test::X64_WINDOWS);
    auto spec_a = spec_map.emplace("a", "b[core]");
    auto spec_b = spec_map.emplace("b", "", {{"b0", ""}, {"b1", ""}}, {"b0", "b1"});

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto plan = Dependencies::create_upgrade_plan(provider, var_provider, {spec_a, spec_b}, status_db);

    // The upgrade should install the new default feature b1 but not b0
    REQUIRE(plan.size() == 4);
    remove_plan_check(plan.remove_actions.at(0), "a", Test::X64_WINDOWS);
    remove_plan_check(plan.remove_actions.at(1), "b", Test::X64_WINDOWS);
    features_check(plan.install_actions.at(0), "b", {"core", "b1"}, Test::X64_WINDOWS);
    features_check(plan.install_actions.at(1), "a", {"core"}, Test::X64_WINDOWS);
}

TEST_CASE ("upgrade with default features 3", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    // note: unrelated package due to x86 triplet
    pghs.push_back(make_status_pgh("b", "", "", "x86-windows"));
    pghs.push_back(make_status_pgh("a", "", "", "x64-windows"));

    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map(Test::X64_WINDOWS);
    auto spec_a = spec_map.emplace("a", "b[core]");
    spec_map.emplace("b", "", {{"b0", ""}, {"b1", ""}}, {"b0"});

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto plan = Dependencies::create_upgrade_plan(provider, var_provider, {spec_a}, status_db);

    // The upgrade should install the default feature
    REQUIRE(plan.size() == 3);
    remove_plan_check(plan.remove_actions.at(0), "a", Test::X64_WINDOWS);
    features_check(plan.install_actions.at(0), "b", {"b0", "core"}, Test::X64_WINDOWS);
    features_check(plan.install_actions.at(1), "a", {"core"}, Test::X64_WINDOWS);
}

TEST_CASE ("upgrade with new default feature", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a", "", "0", "x86-windows"));

    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a", "", {{"0", ""}, {"1", ""}, {"2", ""}}, {"0", "1"});

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto plan = Dependencies::create_upgrade_plan(provider, var_provider, {spec_a}, status_db);

    // The upgrade should install the new default feature but not the old default feature 0
    REQUIRE(plan.size() == 2);
    remove_plan_check(plan.remove_actions.at(0), "a", Test::X86_WINDOWS);
    features_check(plan.install_actions.at(0), "a", {"core", "1"}, Test::X86_WINDOWS);
}

TEST_CASE ("transitive features test", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    PackageSpecMap spec_map(Test::X64_WINDOWS);
    auto spec_a_64 = FullPackageSpec{spec_map.emplace("a", "b", {{"0", "b[0]"}}), {"core"}};
    auto spec_b_64 = FullPackageSpec{spec_map.emplace("b", "c", {{"0", "c[0]"}}), {"core"}};
    auto spec_c_64 = FullPackageSpec{spec_map.emplace("c", "", {{"0", ""}}), {"core"}};

    auto install_specs = FullPackageSpec::from_string("a[*]", Test::X64_WINDOWS);
    REQUIRE(install_specs.has_value());
    if (!install_specs.has_value()) return;

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto install_plan = Dependencies::create_feature_install_plan(provider,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 3);
    features_check(install_plan.install_actions.at(0), "c", {"0", "core"}, Test::X64_WINDOWS);
    features_check(install_plan.install_actions.at(1), "b", {"0", "core"}, Test::X64_WINDOWS);
    features_check(install_plan.install_actions.at(2), "a", {"0", "core"}, Test::X64_WINDOWS);
}

TEST_CASE ("no transitive features test", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    PackageSpecMap spec_map(Test::X64_WINDOWS);
    auto spec_a_64 = FullPackageSpec{spec_map.emplace("a", "b", {{"0", ""}}), {"core"}};
    auto spec_b_64 = FullPackageSpec{spec_map.emplace("b", "c", {{"0", ""}}), {"core"}};
    auto spec_c_64 = FullPackageSpec{spec_map.emplace("c", "", {{"0", ""}}), {"core"}};

    auto install_specs = FullPackageSpec::from_string("a[*]", Test::X64_WINDOWS);
    REQUIRE(install_specs.has_value());
    if (!install_specs.has_value()) return;
    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto install_plan = Dependencies::create_feature_install_plan(provider,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 3);
    features_check(install_plan.install_actions.at(0), "c", {"core"}, Test::X64_WINDOWS);
    features_check(install_plan.install_actions.at(1), "b", {"core"}, Test::X64_WINDOWS);
    features_check(install_plan.install_actions.at(2), "a", {"0", "core"}, Test::X64_WINDOWS);
}

TEST_CASE ("only transitive features test", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    PackageSpecMap spec_map(Test::X64_WINDOWS);
    auto spec_a_64 = FullPackageSpec{spec_map.emplace("a", "", {{"0", "b[0]"}}), {"core"}};
    auto spec_b_64 = FullPackageSpec{spec_map.emplace("b", "", {{"0", "c[0]"}}), {"core"}};
    auto spec_c_64 = FullPackageSpec{spec_map.emplace("c", "", {{"0", ""}}), {"core"}};

    auto install_specs = FullPackageSpec::from_string("a[*]", Test::X64_WINDOWS);
    REQUIRE(install_specs.has_value());
    if (!install_specs.has_value()) return;
    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto install_plan = Dependencies::create_feature_install_plan(provider,
                                                                  var_provider,
                                                                  {&install_specs.value_or_exit(VCPKG_LINE_INFO), 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)));

    REQUIRE(install_plan.size() == 3);
    features_check(install_plan.install_actions.at(0), "c", {"0", "core"}, Test::X64_WINDOWS);
    features_check(install_plan.install_actions.at(1), "b", {"0", "core"}, Test::X64_WINDOWS);
    features_check(install_plan.install_actions.at(2), "a", {"0", "core"}, Test::X64_WINDOWS);
}

TEST_CASE ("basic remove scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    StatusParagraphs status_db(std::move(pghs));

    auto remove_plan = Dependencies::create_remove_plan({{"a", Test::X86_WINDOWS}}, status_db);

    REQUIRE(remove_plan.size() == 1);
    REQUIRE(remove_plan.at(0).spec.name() == "a");
}

TEST_CASE ("recurse remove scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    pghs.push_back(make_status_pgh("b", "a"));
    StatusParagraphs status_db(std::move(pghs));

    auto remove_plan = Dependencies::create_remove_plan({{"a", Test::X86_WINDOWS}}, status_db);

    REQUIRE(remove_plan.size() == 2);
    REQUIRE(remove_plan.at(0).spec.name() == "b");
    REQUIRE(remove_plan.at(1).spec.name() == "a");
}

TEST_CASE ("features depend remove scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    pghs.push_back(make_status_pgh("b"));
    pghs.push_back(make_status_feature_pgh("b", "0", "a"));
    StatusParagraphs status_db(std::move(pghs));

    auto remove_plan = Dependencies::create_remove_plan({{"a", Test::X86_WINDOWS}}, status_db);

    REQUIRE(remove_plan.size() == 2);
    REQUIRE(remove_plan.at(0).spec.name() == "b");
    REQUIRE(remove_plan.at(1).spec.name() == "a");
}

TEST_CASE ("features depend remove scheme once removed", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("expat"));
    pghs.push_back(make_status_pgh("vtk", "expat"));
    pghs.push_back(make_status_pgh("opencv"));
    pghs.push_back(make_status_feature_pgh("opencv", "vtk", "vtk"));
    StatusParagraphs status_db(std::move(pghs));

    auto remove_plan = Dependencies::create_remove_plan({{"expat", Test::X86_WINDOWS}}, status_db);

    REQUIRE(remove_plan.size() == 3);
    REQUIRE(remove_plan.at(0).spec.name() == "opencv");
    REQUIRE(remove_plan.at(1).spec.name() == "vtk");
    REQUIRE(remove_plan.at(2).spec.name() == "expat");
}

TEST_CASE ("features depend remove scheme once removed x64", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("expat", "", "", "x64"));
    pghs.push_back(make_status_pgh("vtk", "expat", "", "x64"));
    pghs.push_back(make_status_pgh("opencv", "", "", "x64"));
    pghs.push_back(make_status_feature_pgh("opencv", "vtk", "vtk", "x64"));
    StatusParagraphs status_db(std::move(pghs));

    auto remove_plan = Dependencies::create_remove_plan({{"expat", Triplet::from_canonical_name("x64")}}, status_db);

    REQUIRE(remove_plan.size() == 3);
    REQUIRE(remove_plan.at(0).spec.name() == "opencv");
    REQUIRE(remove_plan.at(1).spec.name() == "vtk");
    REQUIRE(remove_plan.at(2).spec.name() == "expat");
}

TEST_CASE ("features depend core remove scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("curl", "", "", "x64"));
    pghs.push_back(make_status_pgh("cpr", "curl[core]", "", "x64"));
    StatusParagraphs status_db(std::move(pghs));

    auto remove_plan = Dependencies::create_remove_plan({{"curl", Triplet::from_canonical_name("x64")}}, status_db);

    REQUIRE(remove_plan.size() == 2);
    REQUIRE(remove_plan.at(0).spec.name() == "cpr");
    REQUIRE(remove_plan.at(1).spec.name() == "curl");
}

TEST_CASE ("features depend core remove scheme 2", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("curl", "", "", "x64"));
    pghs.push_back(make_status_feature_pgh("curl", "a", "", "x64"));
    pghs.push_back(make_status_feature_pgh("curl", "b", "curl[a]", "x64"));
    StatusParagraphs status_db(std::move(pghs));

    auto remove_plan = Dependencies::create_remove_plan({{"curl", Triplet::from_canonical_name("x64")}}, status_db);

    REQUIRE(remove_plan.size() == 1);
    REQUIRE(remove_plan.at(0).spec.name() == "curl");
}

TEST_CASE ("self-referencing scheme", "[plan]")
{
    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a", "a");
    auto spec_b = spec_map.emplace("b", "b, b (x64)");

    PortFileProvider::MapPortFileProvider map_port(spec_map.map);
    MockCMakeVarProvider var_provider;

    SECTION ("basic")
    {
        auto fullspec_a = FullPackageSpec{spec_a, {}};
        auto install_plan = Dependencies::create_feature_install_plan(
            map_port, var_provider, {&fullspec_a, 1}, {}, {{}, Test::X64_WINDOWS});

        REQUIRE(install_plan.size() == 1);
        REQUIRE(install_plan.install_actions.at(0).spec == spec_a);
    }
    SECTION ("qualified")
    {
        auto fullspec_b = FullPackageSpec{spec_b, {}};
        auto install_plan = Dependencies::create_feature_install_plan(
            map_port, var_provider, {&fullspec_b, 1}, {}, {{}, Test::X64_WINDOWS});

        REQUIRE(install_plan.size() == 1);
        REQUIRE(install_plan.install_actions.at(0).spec == spec_b);
    }
}

TEST_CASE ("basic tool port scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> status_paragraphs;

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a", "b");
    auto spec_b = spec_map.emplace("b", "c");
    auto spec_c = spec_map.emplace("c");

    spec_map.map.at("a").source_control_file->core_paragraph->dependencies[0].host = true;

    PortFileProvider::MapPortFileProvider map_port(spec_map.map);
    MockCMakeVarProvider var_provider;

    auto fullspec_a = FullPackageSpec{spec_a, {}};
    auto install_plan = Dependencies::create_feature_install_plan(map_port,
                                                                  var_provider,
                                                                  {&fullspec_a, 1},
                                                                  StatusParagraphs(std::move(status_paragraphs)),
                                                                  {{}, Test::X64_WINDOWS});

    REQUIRE(install_plan.size() == 3);
    REQUIRE(install_plan.install_actions.at(0).spec.name() == "c");
    REQUIRE(install_plan.install_actions.at(0).spec.triplet() == Test::X64_WINDOWS);
    REQUIRE(install_plan.install_actions.at(1).spec.name() == "b");
    REQUIRE(install_plan.install_actions.at(1).spec.triplet() == Test::X64_WINDOWS);
    REQUIRE(install_plan.install_actions.at(2).spec.name() == "a");
}

TEST_CASE ("basic existing tool port scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("b", "", "", "x64-windows"));
    StatusParagraphs status_db(std::move(pghs));
    MockCMakeVarProvider var_provider;

    SECTION ("a+b")
    {
        PackageSpecMap spec_map;
        auto spec_a = spec_map.emplace("a", "b");
        auto spec_b = spec_map.emplace("b");

        spec_map.map.at("a").source_control_file->core_paragraph->dependencies[0].host = true;

        PortFileProvider::MapPortFileProvider map_port(spec_map.map);

        auto fullspec_a = FullPackageSpec{spec_a, {}};
        auto install_plan = Dependencies::create_feature_install_plan(
            map_port, var_provider, {&fullspec_a, 1}, status_db, {{}, Test::X64_WINDOWS});

        REQUIRE(install_plan.size() == 1);
        REQUIRE(install_plan.install_actions.at(0).spec == spec_a);
    }

    SECTION ("a recurse")
    {
        PackageSpecMap spec_map;
        auto spec_a = spec_map.emplace("a", "a");

        spec_map.map.at("a").source_control_file->core_paragraph->dependencies[0].host = true;

        PortFileProvider::MapPortFileProvider map_port(spec_map.map);

        auto fullspec_a = FullPackageSpec{spec_a, {}};
        auto install_plan = Dependencies::create_feature_install_plan(
            map_port, var_provider, {&fullspec_a, 1}, status_db, {{}, Test::X64_WINDOWS});

        REQUIRE(install_plan.size() == 2);
        REQUIRE(install_plan.install_actions.at(0).spec.name() == "a");
        REQUIRE(install_plan.install_actions.at(0).spec.triplet() == Test::X64_WINDOWS);
        REQUIRE(install_plan.install_actions.at(1).spec == spec_a);

        install_plan = Dependencies::create_feature_install_plan(
            map_port, var_provider, {&fullspec_a, 1}, status_db, {{}, Test::X86_WINDOWS});

        REQUIRE(install_plan.size() == 1);
        REQUIRE(install_plan.install_actions.at(0).spec == spec_a);
    }

    SECTION ("a+b (arm)")
    {
        PackageSpecMap spec_map;
        auto spec_a = spec_map.emplace("a", "b");
        auto spec_b = spec_map.emplace("b");

        spec_map.map.at("a").source_control_file->core_paragraph->dependencies[0].host = true;

        PortFileProvider::MapPortFileProvider map_port(spec_map.map);

        auto fullspec_a = FullPackageSpec{spec_a, {}};
        auto install_plan = Dependencies::create_feature_install_plan(
            map_port, var_provider, {&fullspec_a, 1}, status_db, {{}, Test::ARM_UWP});

        REQUIRE(install_plan.size() == 2);
        REQUIRE(install_plan.install_actions.at(0).spec.name() == "b");
        REQUIRE(install_plan.install_actions.at(0).spec.triplet() == Test::ARM_UWP);
        REQUIRE(install_plan.install_actions.at(1).spec == spec_a);
    }

    SECTION ("a+b+c")
    {
        PackageSpecMap spec_map;
        auto spec_a = spec_map.emplace("a", "b");
        auto spec_b = spec_map.emplace("b", "c");
        auto spec_c = spec_map.emplace("c");

        spec_map.map.at("a").source_control_file->core_paragraph->dependencies[0].host = true;

        PortFileProvider::MapPortFileProvider map_port(spec_map.map);

        auto fullspec_a = FullPackageSpec{spec_a, {}};
        auto install_plan = Dependencies::create_feature_install_plan(
            map_port, var_provider, {&fullspec_a, 1}, status_db, {{}, Test::X64_WINDOWS});

        REQUIRE(install_plan.size() == 1);
        REQUIRE(install_plan.install_actions.at(0).spec == spec_a);
    }
}

TEST_CASE ("remove tool port scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    StatusParagraphs status_db(std::move(pghs));

    auto remove_plan = Dependencies::create_remove_plan({{"a", Test::X86_WINDOWS}}, status_db);

    REQUIRE(remove_plan.size() == 1);
    REQUIRE(remove_plan.at(0).spec.name() == "a");
}

TEST_CASE ("basic upgrade scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a");

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto plan = Dependencies::create_upgrade_plan(provider, var_provider, {spec_a}, status_db);

    REQUIRE(plan.size() == 2);
    remove_plan_check(plan.remove_actions.at(0), "a");
    features_check(plan.install_actions.at(0), "a", {"core"});
}

TEST_CASE ("basic upgrade scheme with recurse", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    pghs.push_back(make_status_pgh("b", "a"));
    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a");
    spec_map.emplace("b", "a");

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto plan = Dependencies::create_upgrade_plan(provider, var_provider, {spec_a}, status_db);

    REQUIRE(plan.size() == 4);
    remove_plan_check(plan.remove_actions.at(0), "b");
    remove_plan_check(plan.remove_actions.at(1), "a");
    features_check(plan.install_actions.at(0), "a", {"core"});
    features_check(plan.install_actions.at(1), "b", {"core"});
}

TEST_CASE ("basic upgrade scheme with bystander", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    pghs.push_back(make_status_pgh("b"));
    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a");
    spec_map.emplace("b", "a");

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto plan = Dependencies::create_upgrade_plan(provider, var_provider, {spec_a}, status_db);

    REQUIRE(plan.size() == 2);
    remove_plan_check(plan.remove_actions.at(0), "a");
    features_check(plan.install_actions.at(0), "a", {"core"});
}

TEST_CASE ("basic upgrade scheme with new dep", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a", "b");
    spec_map.emplace("b");

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto plan = Dependencies::create_upgrade_plan(provider, var_provider, {spec_a}, status_db);

    REQUIRE(plan.size() == 3);
    remove_plan_check(plan.remove_actions.at(0), "a");
    features_check(plan.install_actions.at(0), "b", {"core"});
    features_check(plan.install_actions.at(1), "a", {"core"});
}

TEST_CASE ("basic upgrade scheme with features", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    pghs.push_back(make_status_feature_pgh("a", "a1"));
    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a", "", {{"a1", ""}});

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto plan = Dependencies::create_upgrade_plan(provider, var_provider, {spec_a}, status_db);

    REQUIRE(plan.size() == 2);
    remove_plan_check(plan.remove_actions.at(0), "a");
    features_check(plan.install_actions.at(0), "a", {"core", "a1"});
}

TEST_CASE ("basic upgrade scheme with new default feature", "[plan]")
{
    // only core of package "a" is installed
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    StatusParagraphs status_db(std::move(pghs));

    // a1 was added as a default feature and should be installed in upgrade
    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a", "", {{"a1", ""}}, {"a1"});

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto plan = Dependencies::create_upgrade_plan(provider, var_provider, {spec_a}, status_db);

    REQUIRE(plan.size() == 2);
    remove_plan_check(plan.remove_actions.at(0), "a");
    features_check(plan.install_actions.at(0), "a", {"core", "a1"});
}

TEST_CASE ("basic upgrade scheme with self features", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    pghs.push_back(make_status_feature_pgh("a", "a1", ""));
    pghs.push_back(make_status_feature_pgh("a", "a2", "a[a1]"));
    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a", "", {{"a1", ""}, {"a2", "a[a1]"}});

    PortFileProvider::MapPortFileProvider provider(spec_map.map);
    MockCMakeVarProvider var_provider;
    auto plan = Dependencies::create_upgrade_plan(provider, var_provider, {spec_a}, status_db);

    REQUIRE(plan.size() == 2);
    remove_plan_check(plan.remove_actions.at(0), "a");
    features_check(plan.install_actions.at(0), "a", {"a1", "a2", "core"});
}

TEST_CASE ("basic export scheme", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a");

    auto plan = Dependencies::create_export_plan({spec_a}, status_db);

    REQUIRE(plan.size() == 1);
    REQUIRE(plan.at(0).spec.name() == "a");
    REQUIRE(plan.at(0).plan_type == Dependencies::ExportPlanType::ALREADY_BUILT);
}

TEST_CASE ("basic export scheme with recurse", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    pghs.push_back(make_status_pgh("b", "a"));
    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a");
    auto spec_b = spec_map.emplace("b", "a");

    auto plan = Dependencies::create_export_plan({spec_b}, status_db);

    REQUIRE(plan.size() == 2);
    REQUIRE(plan.at(0).spec.name() == "a");
    REQUIRE(plan.at(0).plan_type == Dependencies::ExportPlanType::ALREADY_BUILT);

    REQUIRE(plan.at(1).spec.name() == "b");
    REQUIRE(plan.at(1).plan_type == Dependencies::ExportPlanType::ALREADY_BUILT);
}

TEST_CASE ("basic export scheme with bystander", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("a"));
    pghs.push_back(make_status_pgh("b"));
    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a");
    auto spec_b = spec_map.emplace("b", "a");

    auto plan = Dependencies::create_export_plan({spec_a}, status_db);

    REQUIRE(plan.size() == 1);
    REQUIRE(plan.at(0).spec.name() == "a");
    REQUIRE(plan.at(0).plan_type == Dependencies::ExportPlanType::ALREADY_BUILT);
}

TEST_CASE ("basic export scheme with missing", "[plan]")
{
    StatusParagraphs status_db;

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a");

    auto plan = Dependencies::create_export_plan({spec_a}, status_db);

    REQUIRE(plan.size() == 1);
    REQUIRE(plan.at(0).spec.name() == "a");
    REQUIRE(plan.at(0).plan_type == Dependencies::ExportPlanType::NOT_BUILT);
}

TEST_CASE ("basic export scheme with features", "[plan]")
{
    std::vector<std::unique_ptr<StatusParagraph>> pghs;
    pghs.push_back(make_status_pgh("b"));
    pghs.push_back(make_status_pgh("a"));
    pghs.push_back(make_status_feature_pgh("a", "a1", "b[core]"));
    StatusParagraphs status_db(std::move(pghs));

    PackageSpecMap spec_map;
    auto spec_a = spec_map.emplace("a", "", {{"a1", ""}});

    auto plan = Dependencies::create_export_plan({spec_a}, status_db);

    REQUIRE(plan.size() == 2);

    REQUIRE(plan.at(0).spec.name() == "b");
    REQUIRE(plan.at(0).plan_type == Dependencies::ExportPlanType::ALREADY_BUILT);

    REQUIRE(plan.at(1).spec.name() == "a");
    REQUIRE(plan.at(1).plan_type == Dependencies::ExportPlanType::ALREADY_BUILT);
}
