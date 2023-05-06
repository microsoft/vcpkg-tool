#include <catch2/catch.hpp>

#include <vcpkg/base/files.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/binarycaching.private.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <string>

#include <vcpkg-test/util.h>

using namespace vcpkg;

struct KnowNothingBinaryProvider : IReadBinaryProvider
{
    void fetch(View<const InstallPlanAction*> actions, Span<RestoreResult> out_status) const override
    {
        REQUIRE(actions.size() == out_status.size());
        for (size_t idx = 0; idx < out_status.size(); ++idx)
        {
            CHECK(actions[idx]->has_package_abi());
            CHECK(out_status[idx] == RestoreResult::unavailable);
        }
    }
    void precheck(View<const InstallPlanAction*> actions, Span<CacheAvailability> out_status) const override
    {
        REQUIRE(actions.size() == out_status.size());
        for (const auto c : out_status)
        {
            REQUIRE(c == CacheAvailability::unknown);
        }
    }

    StringView vendor() const override { return "KnowNothing"; }
};

TEST_CASE ("CacheStatus operations", "[BinaryCache]")
{
    KnowNothingBinaryProvider know_nothing;

    // CacheStatus() noexcept;
    CacheStatus default_constructed;
    REQUIRE(default_constructed.should_attempt_precheck(&know_nothing));
    REQUIRE(default_constructed.should_attempt_restore(&know_nothing));
    REQUIRE(!default_constructed.is_unavailable(&know_nothing));
    REQUIRE(default_constructed.get_available_provider() == nullptr);
    REQUIRE(!default_constructed.is_restored());

    CacheStatus unavailable;
    unavailable.mark_unavailable(&know_nothing);
    REQUIRE(!unavailable.should_attempt_precheck(&know_nothing));
    REQUIRE(!unavailable.should_attempt_restore(&know_nothing));
    REQUIRE(unavailable.is_unavailable(&know_nothing));
    REQUIRE(unavailable.get_available_provider() == nullptr);
    REQUIRE(!unavailable.is_restored());

    CacheStatus available;
    available.mark_available(&know_nothing);
    REQUIRE(!available.should_attempt_precheck(&know_nothing));
    REQUIRE(available.should_attempt_restore(&know_nothing));
    REQUIRE(!available.is_unavailable(&know_nothing));
    REQUIRE(available.get_available_provider() == &know_nothing);
    REQUIRE(!available.is_restored());

    CacheStatus restored;
    restored.mark_restored();
    REQUIRE(!restored.should_attempt_precheck(&know_nothing));
    REQUIRE(!restored.should_attempt_restore(&know_nothing));
    REQUIRE(!restored.is_unavailable(&know_nothing));
    REQUIRE(restored.get_available_provider() == nullptr);
    REQUIRE(restored.is_restored());

    // CacheStatus(const CacheStatus&);
    CacheStatus default_copy{default_constructed};
    REQUIRE(!default_copy.is_unavailable(&know_nothing));

    CacheStatus unavailable_copy{unavailable};
    REQUIRE(!unavailable_copy.should_attempt_precheck(&know_nothing));
    REQUIRE(!unavailable_copy.should_attempt_restore(&know_nothing));
    REQUIRE(unavailable_copy.is_unavailable(&know_nothing));
    REQUIRE(unavailable_copy.get_available_provider() == nullptr);
    REQUIRE(!unavailable_copy.is_restored());

    CacheStatus available_copy{available};
    REQUIRE(!available_copy.should_attempt_precheck(&know_nothing));
    REQUIRE(available_copy.should_attempt_restore(&know_nothing));
    REQUIRE(!available_copy.is_unavailable(&know_nothing));
    REQUIRE(available_copy.get_available_provider() == &know_nothing);
    REQUIRE(!available_copy.is_restored());

    CacheStatus restored_copy{restored};
    REQUIRE(!restored_copy.should_attempt_precheck(&know_nothing));
    REQUIRE(!restored_copy.should_attempt_restore(&know_nothing));
    REQUIRE(!restored_copy.is_unavailable(&know_nothing));
    REQUIRE(restored_copy.get_available_provider() == nullptr);
    REQUIRE(restored_copy.is_restored());

    // CacheStatus(CacheStatus&&) noexcept;
    CacheStatus default_move{std::move(default_copy)};
    REQUIRE(!default_move.is_unavailable(&know_nothing));

    CacheStatus unavailable_move{std::move(unavailable_copy)};
    REQUIRE(!unavailable_move.should_attempt_precheck(&know_nothing));
    REQUIRE(!unavailable_move.should_attempt_restore(&know_nothing));
    REQUIRE(unavailable_move.is_unavailable(&know_nothing));
    REQUIRE(unavailable_move.get_available_provider() == nullptr);
    REQUIRE(!unavailable_move.is_restored());

    CacheStatus available_move{std::move(available_copy)};
    REQUIRE(!available_move.should_attempt_precheck(&know_nothing));
    REQUIRE(available_move.should_attempt_restore(&know_nothing));
    REQUIRE(!available_move.is_unavailable(&know_nothing));
    REQUIRE(available_move.get_available_provider() == &know_nothing);
    REQUIRE(!available_move.is_restored());

    CacheStatus restored_move{std::move(restored_copy)};
    REQUIRE(!restored_move.should_attempt_precheck(&know_nothing));
    REQUIRE(!restored_move.should_attempt_restore(&know_nothing));
    REQUIRE(!restored_move.is_unavailable(&know_nothing));
    REQUIRE(restored_move.get_available_provider() == nullptr);
    REQUIRE(restored_move.is_restored());

    // CacheStatus& operator=(const CacheStatus&);
    CacheStatus assignee;
    assignee = unavailable;
    REQUIRE(!assignee.should_attempt_precheck(&know_nothing));
    REQUIRE(!assignee.should_attempt_restore(&know_nothing));
    REQUIRE(assignee.is_unavailable(&know_nothing));
    REQUIRE(assignee.get_available_provider() == nullptr);
    REQUIRE(!assignee.is_restored());
    assignee = available;
    REQUIRE(!assignee.should_attempt_precheck(&know_nothing));
    REQUIRE(assignee.should_attempt_restore(&know_nothing));
    REQUIRE(!assignee.is_unavailable(&know_nothing));
    REQUIRE(assignee.get_available_provider() == &know_nothing);
    REQUIRE(!assignee.is_restored());
    assignee = restored;
    REQUIRE(!assignee.should_attempt_precheck(&know_nothing));
    REQUIRE(!assignee.should_attempt_restore(&know_nothing));
    REQUIRE(!assignee.is_unavailable(&know_nothing));
    REQUIRE(assignee.get_available_provider() == nullptr);
    REQUIRE(assignee.is_restored());

    // CacheStatus& operator=(CacheStatus&&) noexcept;
    assignee = std::move(unavailable);
    REQUIRE(!assignee.should_attempt_precheck(&know_nothing));
    REQUIRE(!assignee.should_attempt_restore(&know_nothing));
    REQUIRE(assignee.is_unavailable(&know_nothing));
    REQUIRE(assignee.get_available_provider() == nullptr);
    REQUIRE(!assignee.is_restored());
    assignee = std::move(available);
    REQUIRE(!assignee.should_attempt_precheck(&know_nothing));
    REQUIRE(assignee.should_attempt_restore(&know_nothing));
    REQUIRE(!assignee.is_unavailable(&know_nothing));
    REQUIRE(assignee.get_available_provider() == &know_nothing);
    REQUIRE(!assignee.is_restored());
    assignee = std::move(restored);
    REQUIRE(!assignee.should_attempt_precheck(&know_nothing));
    REQUIRE(!assignee.should_attempt_restore(&know_nothing));
    REQUIRE(!assignee.is_unavailable(&know_nothing));
    REQUIRE(assignee.get_available_provider() == nullptr);
    REQUIRE(assignee.is_restored());
}

TEST_CASE ("format_version_for_nugetref semver-ish", "[format_version_for_nugetref]")
{
    REQUIRE(format_version_for_nugetref("0.0.0", "abitag") == "0.0.0-vcpkgabitag");
    REQUIRE(format_version_for_nugetref("1.0.1", "abitag") == "1.0.1-vcpkgabitag");
    REQUIRE(format_version_for_nugetref("1.01.000", "abitag") == "1.1.0-vcpkgabitag");
    REQUIRE(format_version_for_nugetref("1.2", "abitag") == "1.2.0-vcpkgabitag");
    REQUIRE(format_version_for_nugetref("v52", "abitag") == "52.0.0-vcpkgabitag");
    REQUIRE(format_version_for_nugetref("v09.01.02", "abitag") == "9.1.2-vcpkgabitag");
    REQUIRE(format_version_for_nugetref("1.1.1q", "abitag") == "1.1.1-vcpkgabitag");
    REQUIRE(format_version_for_nugetref("1", "abitag") == "1.0.0-vcpkgabitag");
}

TEST_CASE ("format_version_for_nugetref date", "[format_version_for_nugetref]")
{
    REQUIRE(format_version_for_nugetref("2020-06-26", "abitag") == "2020.6.26-vcpkgabitag");
    REQUIRE(format_version_for_nugetref("20-06-26", "abitag") == "0.0.0-vcpkgabitag");
    REQUIRE(format_version_for_nugetref("2020-06-26-release", "abitag") == "2020.6.26-vcpkgabitag");
    REQUIRE(format_version_for_nugetref("2020-06-26000", "abitag") == "2020.6.26-vcpkgabitag");
}

TEST_CASE ("format_version_for_nugetref generic", "[format_version_for_nugetref]")
{
    REQUIRE(format_version_for_nugetref("apr", "abitag") == "0.0.0-vcpkgabitag");
    REQUIRE(format_version_for_nugetref("", "abitag") == "0.0.0-vcpkgabitag");
}

TEST_CASE ("generate_nuspec", "[generate_nuspec]")
{
    const Path pkgPath = "/zlib2_x64-windows";
    const auto pkgPathWild = (pkgPath / "**").native();

    auto pghs = Paragraphs::parse_paragraphs(R"(
Source: zlib2
Version: 1.5
Build-Depends: zlib
Description: a spiffy compression library wrapper

Feature: a
Description: a feature

Feature: b
Description: enable bzip capabilities
Build-Depends: bzip
)",
                                             "<testdata>");
    REQUIRE(pghs.has_value());
    auto maybe_scf = SourceControlFile::parse_control_file("", std::move(*pghs.get()));
    REQUIRE(maybe_scf.has_value());
    SourceControlFileAndLocation scfl{std::move(*maybe_scf.get()), Path()};

    InstallPlanAction ipa(PackageSpec{"zlib2", Test::X64_WINDOWS},
                          scfl,
                          RequestType::USER_REQUESTED,
                          Test::ARM_UWP,
                          {{"a", {}}, {"b", {}}},
                          {});

    ipa.abi_info = AbiInfo{};
    ipa.abi_info.get()->package_abi = "packageabi";
    std::string tripletabi("tripletabi");
    ipa.abi_info.get()->triplet_abi = tripletabi;
    CompilerInfo compiler_info;
    compiler_info.hash = "compilerhash";
    compiler_info.id = "compilerid";
    compiler_info.version = "compilerversion";
    ipa.abi_info.get()->compiler_info = compiler_info;

    NugetReference ref2 = make_nugetref(ipa, "prefix_");

    REQUIRE(ref2.nupkg_filename() == "prefix_zlib2_x64-windows.1.5.0-vcpkgpackageabi.nupkg");

    NugetReference ref = make_nugetref(ipa, "");

    REQUIRE(ref.nupkg_filename() == "zlib2_x64-windows.1.5.0-vcpkgpackageabi.nupkg");

    REQUIRE_LINES(generate_nuspec(pkgPath, ipa, "", {}),
                  R"(<package>
  <metadata>
    <id>zlib2_x64-windows</id>
    <version>1.5.0-vcpkgpackageabi</version>
    <authors>vcpkg</authors>
    <description>NOT FOR DIRECT USE. Automatically generated cache package.

a spiffy compression library wrapper

Version: 1.5
Triplet: x64-windows
CXX Compiler id: compilerid
CXX Compiler version: compilerversion
Triplet/Compiler hash: tripletabi
Features: a, b
Dependencies:
</description>
    <packageTypes><packageType name="vcpkg"/></packageTypes>
  </metadata>
  <files><file src=")" +
                      pkgPathWild +
                      R"(" target=""/></files>
</package>
)");

    REQUIRE_LINES(generate_nuspec(pkgPath, ipa, "", {"urlvalue"}),
                  R"(<package>
  <metadata>
    <id>zlib2_x64-windows</id>
    <version>1.5.0-vcpkgpackageabi</version>
    <authors>vcpkg</authors>
    <description>NOT FOR DIRECT USE. Automatically generated cache package.

a spiffy compression library wrapper

Version: 1.5
Triplet: x64-windows
CXX Compiler id: compilerid
CXX Compiler version: compilerversion
Triplet/Compiler hash: tripletabi
Features: a, b
Dependencies:
</description>
    <packageTypes><packageType name="vcpkg"/></packageTypes>
    <repository type="git" url="urlvalue"/>
  </metadata>
  <files><file src=")" +
                      pkgPathWild +
                      R"(" target=""/></files>
</package>
)");
    REQUIRE_LINES(generate_nuspec(pkgPath, ipa, "", {"urlvalue", "branchvalue", "commitvalue"}),
                  R"(<package>
  <metadata>
    <id>zlib2_x64-windows</id>
    <version>1.5.0-vcpkgpackageabi</version>
    <authors>vcpkg</authors>
    <description>NOT FOR DIRECT USE. Automatically generated cache package.

a spiffy compression library wrapper

Version: 1.5
Triplet: x64-windows
CXX Compiler id: compilerid
CXX Compiler version: compilerversion
Triplet/Compiler hash: tripletabi
Features: a, b
Dependencies:
</description>
    <packageTypes><packageType name="vcpkg"/></packageTypes>
    <repository type="git" url="urlvalue" branch="branchvalue" commit="commitvalue"/>
  </metadata>
  <files><file src=")" +
                      pkgPathWild +
                      R"(" target=""/></files>
</package>
)");
}

TEST_CASE ("Provider nullptr checks", "[BinaryCache]")
{
    // create a binary cache to test
    BinaryProviders providers;
    providers.read.emplace_back(std::make_unique<KnowNothingBinaryProvider>());
    ReadOnlyBinaryCache uut(std::move(providers));

    // create an action plan with an action without a package ABI set
    auto pghs = Paragraphs::parse_paragraphs(R"(
Source: someheadpackage
Version: 1.5
Description:
)",
                                             "<testdata>");
    REQUIRE(pghs.has_value());
    auto maybe_scf = SourceControlFile::parse_control_file("", std::move(*pghs.get()));
    REQUIRE(maybe_scf.has_value());
    SourceControlFileAndLocation scfl{std::move(*maybe_scf.get()), Path()};
    std::vector<InstallPlanAction> install_plan;
    install_plan.emplace_back(PackageSpec{"someheadpackage", Test::X64_WINDOWS},
                              scfl,
                              RequestType::USER_REQUESTED,
                              Test::ARM_UWP,
                              std::map<std::string, std::vector<FeatureSpec>>{},
                              std::vector<LocalizedString>{});
    InstallPlanAction& ipa_without_abi = install_plan.back();
    ipa_without_abi.package_dir = "pkgs/someheadpackage";

    // test that the binary cache does the right thing. See also CHECKs etc. in KnowNothingBinaryProvider
    uut.fetch(install_plan); // should have no effects
}

TEST_CASE ("XmlSerializer", "[XmlSerializer]")
{
    XmlSerializer xml;
    xml.open_tag("a");
    xml.open_tag("b");
    xml.simple_tag("c", "d");
    xml.close_tag("b");
    xml.text("escaping: & < > \" '");

    REQUIRE(xml.buf == R"(<a><b><c>d</c></b>escaping: &amp; &lt; &gt; &quot; &apos;)");

    xml = XmlSerializer();
    xml.emit_declaration();
    xml.start_complex_open_tag("a")
        .text_attr("b", "<")
        .text_attr("c", "  ")
        .finish_self_closing_complex_tag()
        .line_break();
    xml.simple_tag("d", "e");
    REQUIRE(xml.buf == R"(<?xml version="1.0" encoding="utf-8"?><a b="&lt;" c="  "/>)"
                       "\n<d>e</d>");

    xml = XmlSerializer();
    xml.start_complex_open_tag("a").finish_complex_open_tag();
    REQUIRE(xml.buf == R"(<a>)");

    xml = XmlSerializer();
    xml.line_break();
    xml.open_tag("a").line_break().line_break();
    xml.close_tag("a").line_break().line_break();
    REQUIRE(xml.buf == "\n<a>\n\n</a>\n\n");

    xml = XmlSerializer();
    xml.start_complex_open_tag("a")
        .text_attr("b", "<")
        .line_break()
        .text_attr("c", "  ")
        .finish_complex_open_tag()
        .line_break();
    xml.simple_tag("d", "e").line_break();
    REQUIRE(xml.buf == "<a b=\"&lt;\"\n  c=\"  \">\n  <d>e</d>\n");
}

TEST_CASE ("generate_nuget_packages_config", "[generate_nuget_packages_config]")
{
    ActionPlan plan;
    auto packageconfig = generate_nuget_packages_config(plan, "");
    REQUIRE(packageconfig == R"(<?xml version="1.0" encoding="utf-8"?>
<packages>
</packages>
)");

    auto pghs = Paragraphs::parse_paragraphs(R"(
Source: zlib
Version: 1.5
Description: a spiffy compression library wrapper
)",
                                             "<testdata>");
    REQUIRE(pghs.has_value());
    auto maybe_scf = SourceControlFile::parse_control_file("", std::move(*pghs.get()));
    REQUIRE(maybe_scf.has_value());
    SourceControlFileAndLocation scfl{std::move(*maybe_scf.get()), Path()};
    plan.install_actions.emplace_back(PackageSpec("zlib", Test::X64_ANDROID),
                                      scfl,
                                      RequestType::USER_REQUESTED,
                                      Test::ARM64_WINDOWS,
                                      std::map<std::string, std::vector<FeatureSpec>>{},
                                      std::vector<LocalizedString>{});
    plan.install_actions[0].abi_info = AbiInfo{};
    plan.install_actions[0].abi_info.get()->package_abi = "packageabi";

    packageconfig = generate_nuget_packages_config(plan, "");
    REQUIRE(packageconfig == R"(<?xml version="1.0" encoding="utf-8"?>
<packages>
  <package id="zlib_x64-android" version="1.5.0-vcpkgpackageabi"/>
</packages>
)");

    auto pghs2 = Paragraphs::parse_paragraphs(R"(
Source: zlib2
Version: 1.52
Description: a spiffy compression library wrapper
)",
                                              "<testdata>");
    REQUIRE(pghs2.has_value());
    auto maybe_scf2 = SourceControlFile::parse_control_file("", std::move(*pghs2.get()));
    REQUIRE(maybe_scf2.has_value());
    SourceControlFileAndLocation scfl2{std::move(*maybe_scf2.get()), Path()};
    plan.install_actions.emplace_back(PackageSpec("zlib2", Test::X64_ANDROID),
                                      scfl2,
                                      RequestType::USER_REQUESTED,
                                      Test::ARM64_WINDOWS,
                                      std::map<std::string, std::vector<FeatureSpec>>{},
                                      std::vector<LocalizedString>{});
    plan.install_actions[1].abi_info = AbiInfo{};
    plan.install_actions[1].abi_info.get()->package_abi = "packageabi2";

    packageconfig = generate_nuget_packages_config(plan, "");
    REQUIRE_LINES(packageconfig, R"(<?xml version="1.0" encoding="utf-8"?>
<packages>
  <package id="zlib_x64-android" version="1.5.0-vcpkgpackageabi"/>
  <package id="zlib2_x64-android" version="1.52.0-vcpkgpackageabi2"/>
</packages>
)");
}
