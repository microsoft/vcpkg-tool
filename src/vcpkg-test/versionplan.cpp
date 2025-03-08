#include <vcpkg-test/util.h>

#include <vcpkg/dependencies.h>
#include <vcpkg/paragraphparser.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/sourceparagraph.h>

#include <vcpkg-test/mockcmakevarprovider.h>

using namespace vcpkg;

TEST_CASE ("parse depends", "[dependencies]")
{
    auto w = parse_dependencies_list("liba (windows)", "<test>");
    REQUIRE(w);
    auto& v = *w.get();
    REQUIRE(v.size() == 1);
    REQUIRE(v.at(0).name == "liba");
    REQUIRE(v.at(0).platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    REQUIRE(v.at(0).platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
    REQUIRE(!v.at(0).platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));
}

TEST_CASE ("filter depends", "[dependencies]")
{
    const std::vector<std::string> defaults{"core", "default"};

    const std::unordered_map<std::string, std::string> x64_win_cmake_vars{{"VCPKG_TARGET_ARCHITECTURE", "x64"},
                                                                          {"VCPKG_CMAKE_SYSTEM_NAME", ""}};

    const std::unordered_map<std::string, std::string> arm_uwp_cmake_vars{{"VCPKG_TARGET_ARCHITECTURE", "arm"},
                                                                          {"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}};
    auto deps_ = parse_dependencies_list("liba (!uwp), libb, libc (uwp)", "<test>");
    REQUIRE(deps_);
    auto& deps = *deps_.get();
    SECTION ("x64-windows")
    {
        auto v = filter_dependencies(deps, Test::X64_WINDOWS, Test::X86_WINDOWS, x64_win_cmake_vars);
        REQUIRE(v.size() == 2);
        REQUIRE(v.at(0).package_spec.name() == "liba");
        REQUIRE(v.at(0).features == defaults);
        REQUIRE(v.at(1).package_spec.name() == "libb");
        REQUIRE(v.at(1).features == defaults);
    }

    SECTION ("arm-uwp")
    {
        auto v2 = filter_dependencies(deps, Test::ARM_UWP, Test::X86_WINDOWS, arm_uwp_cmake_vars);
        REQUIRE(v2.size() == 2);
        REQUIRE(v2.at(0).package_spec.name() == "libb");
        REQUIRE(v2.at(0).features == defaults);
        REQUIRE(v2.at(1).package_spec.name() == "libc");
        REQUIRE(v2.at(1).features == defaults);
    }
}

TEST_CASE ("parse feature depends", "[dependencies]")
{
    auto u_ = parse_dependencies_list("libwebp[anim, gif2webp, img2webp, info, mux, nearlossless, "
                                      "simd, cwebp, dwebp], libwebp[vwebp-sdl, extras] (!osx)",
                                      "<test>");
    REQUIRE(u_);
    auto& v = *u_.get();
    REQUIRE(v.size() == 2);
    auto&& a0 = v.at(0);
    REQUIRE(a0.name == "libwebp");
    REQUIRE(a0.features.size() == 9);
    REQUIRE(a0.platform.is_empty());

    auto&& a1 = v.at(1);
    REQUIRE(a1.name == "libwebp");
    REQUIRE(a1.features.size() == 2);
    REQUIRE(!a1.platform.is_empty());
    REQUIRE(a1.platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    REQUIRE(a1.platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    REQUIRE_FALSE(a1.platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));
}

TEST_CASE ("qualified dependency", "[dependencies]")
{
    using namespace Test;
    PackageSpecMap spec_map;
    spec_map.emplace("a", "b, b[b1] (linux)");
    spec_map.emplace("b", "", {{"b1", ""}});

    MapPortFileProvider map_port{spec_map.map};
    MockCMakeVarProvider var_provider;
    var_provider.dep_info_vars[{"a", Test::X64_LINUX}].emplace("VCPKG_CMAKE_SYSTEM_NAME", "Linux");

    PackagesDirAssigner packages_dir_assigner{"pkg"};
    const CreateInstallPlanOptions create_options{
        nullptr, Test::X64_ANDROID, UnsupportedPortAction::Error, UseHeadVersion::No, Editable::No};

    auto plan = vcpkg::create_feature_install_plan(
        map_port, var_provider, Test::parse_test_fspecs("a"), {}, packages_dir_assigner, create_options);
    REQUIRE(plan.install_actions.size() == 2);
    REQUIRE(plan.install_actions.at(0).feature_list == std::vector<std::string>{"core"});
    REQUIRE(plan.install_actions[0].package_dir == "pkg" VCPKG_PREFERRED_SEPARATOR "b_x86-windows");

    auto plan2 = vcpkg::create_feature_install_plan(
        map_port, var_provider, Test::parse_test_fspecs("a:x64-linux"), {}, packages_dir_assigner, create_options);
    REQUIRE(plan2.install_actions.size() == 2);
    REQUIRE(plan2.install_actions[0].feature_list == std::vector<std::string>{"b1", "core"});
    REQUIRE(plan2.install_actions[0].package_dir == "pkg" VCPKG_PREFERRED_SEPARATOR "b_x64-linux");
}
