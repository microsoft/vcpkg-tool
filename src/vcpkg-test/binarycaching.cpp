#include <vcpkg-test/util.h>

#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/binarycaching.private.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/sourceparagraph.h>

#include <string>

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

    LocalizedString restored_message(size_t, std::chrono::high_resolution_clock::duration) const override
    {
        return LocalizedString::from_raw("Nothing");
    }
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

TEST_CASE ("format_version_for_feedref semver-ish", "[format_version_for_feedref]")
{
    REQUIRE(format_version_for_feedref("0.0.0", "abitag") == "0.0.0-vcpkgabitag");
    REQUIRE(format_version_for_feedref("1.0.1", "abitag") == "1.0.1-vcpkgabitag");
    REQUIRE(format_version_for_feedref("1.01.000", "abitag") == "1.1.0-vcpkgabitag");
    REQUIRE(format_version_for_feedref("1.2", "abitag") == "1.2.0-vcpkgabitag");
    REQUIRE(format_version_for_feedref("v52", "abitag") == "52.0.0-vcpkgabitag");
    REQUIRE(format_version_for_feedref("v09.01.02", "abitag") == "9.1.2-vcpkgabitag");
    REQUIRE(format_version_for_feedref("1.1.1q", "abitag") == "1.1.1-vcpkgabitag");
    REQUIRE(format_version_for_feedref("1", "abitag") == "1.0.0-vcpkgabitag");
}

TEST_CASE ("format_version_for_feedref date", "[format_version_for_feedref]")
{
    REQUIRE(format_version_for_feedref("2020-06-26", "abitag") == "2020.6.26-vcpkgabitag");
    REQUIRE(format_version_for_feedref("20-06-26", "abitag") == "0.0.0-vcpkgabitag");
    REQUIRE(format_version_for_feedref("2020-06-26-release", "abitag") == "2020.6.26-vcpkgabitag");
    REQUIRE(format_version_for_feedref("2020-06-26000", "abitag") == "2020.6.26-vcpkgabitag");
}

TEST_CASE ("format_version_for_feedref generic", "[format_version_for_feedref]")
{
    REQUIRE(format_version_for_feedref("apr", "abitag") == "0.0.0-vcpkgabitag");
    REQUIRE(format_version_for_feedref("", "abitag") == "0.0.0-vcpkgabitag");
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
    auto maybe_scf = SourceControlFile::parse_control_file("test-origin", std::move(*pghs.get()));
    REQUIRE(maybe_scf.has_value());
    SourceControlFileAndLocation scfl{std::move(*maybe_scf.get()), Path()};

    PackagesDirAssigner packages_dir_assigner{"test_packages_root"};
    InstallPlanAction ipa(PackageSpec{"zlib2", Test::X64_WINDOWS},
                          scfl,
                          packages_dir_assigner,
                          RequestType::USER_REQUESTED,
                          UseHeadVersion::No,
                          Editable::No,
                          {{"a", {}}, {"b", {}}},
                          {},
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

    FeedReference ref2 = make_nugetref(ipa, "prefix_");

    REQUIRE(ref2.nupkg_filename() == "prefix_zlib2_x64-windows.1.5.0-vcpkgpackageabi.nupkg");

    FeedReference ref = make_nugetref(ipa, "");

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
    ReadOnlyBinaryCache uut;
    uut.install_read_provider(std::make_unique<KnowNothingBinaryProvider>());

    // create an action plan with an action without a package ABI set
    auto pghs = Paragraphs::parse_paragraphs(R"(
Source: someheadpackage
Version: 1.5
Description:
)",
                                             "<testdata>");
    REQUIRE(pghs.has_value());
    auto maybe_scf = SourceControlFile::parse_control_file("test-origin", std::move(*pghs.get()));
    REQUIRE(maybe_scf.has_value());
    SourceControlFileAndLocation scfl{std::move(*maybe_scf.get()), Path()};
    std::vector<InstallPlanAction> install_plan;
    PackagesDirAssigner packages_dir_assigner{"test_packages_root"};
    install_plan.emplace_back(PackageSpec{"someheadpackage", Test::X64_WINDOWS},
                              scfl,
                              packages_dir_assigner,
                              RequestType::USER_REQUESTED,
                              UseHeadVersion::No,
                              Editable::No,
                              std::map<std::string, std::vector<FeatureSpec>>{},
                              std::vector<LocalizedString>{},
                              std::vector<std::string>{});
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
    auto maybe_scf = SourceControlFile::parse_control_file("test-origin", std::move(*pghs.get()));
    REQUIRE(maybe_scf.has_value());
    SourceControlFileAndLocation scfl{std::move(*maybe_scf.get()), Path()};
    PackagesDirAssigner packages_dir_assigner{"test_packages_root"};
    plan.install_actions.emplace_back(PackageSpec("zlib", Test::X64_ANDROID),
                                      scfl,
                                      packages_dir_assigner,
                                      RequestType::USER_REQUESTED,
                                      UseHeadVersion::No,
                                      Editable::No,
                                      std::map<std::string, std::vector<FeatureSpec>>{},
                                      std::vector<LocalizedString>{},
                                      std::vector<std::string>{});
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
    auto maybe_scf2 = SourceControlFile::parse_control_file("test-origin", std::move(*pghs2.get()));
    REQUIRE(maybe_scf2.has_value());
    SourceControlFileAndLocation scfl2{std::move(*maybe_scf2.get()), Path()};
    plan.install_actions.emplace_back(PackageSpec("zlib2", Test::X64_ANDROID),
                                      scfl2,
                                      packages_dir_assigner,
                                      RequestType::USER_REQUESTED,
                                      UseHeadVersion::No,
                                      Editable::No,
                                      std::map<std::string, std::vector<FeatureSpec>>{},
                                      std::vector<LocalizedString>{},
                                      std::vector<std::string>{});
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

TEST_CASE ("Synchronizer operations", "[BinaryCache]")
{
    {
        BinaryCacheSynchronizer sync;
        auto result = sync.fetch_add_completed();
        REQUIRE(result.jobs_submitted == 0);
        REQUIRE(result.jobs_completed == 1);
        REQUIRE_FALSE(result.submission_complete);
    }

    {
        BinaryCacheSynchronizer sync;
        sync.add_submitted();
        sync.add_submitted();
        auto result = sync.fetch_add_completed();
        REQUIRE(result.jobs_submitted == 2);
        REQUIRE(result.jobs_completed == 1);
        REQUIRE_FALSE(result.submission_complete);
    }

    {
        BinaryCacheSynchronizer sync;
        sync.add_submitted();
        REQUIRE(sync.fetch_incomplete_mark_submission_complete() == 1);
        sync.add_submitted();
        REQUIRE(sync.fetch_incomplete_mark_submission_complete() == 2);
        auto result = sync.fetch_add_completed();
        REQUIRE(result.jobs_submitted == 2);
        REQUIRE(result.jobs_completed == 1);
        REQUIRE(result.submission_complete);
        result = sync.fetch_add_completed();
        REQUIRE(result.jobs_submitted == 2);
        REQUIRE(result.jobs_completed == 2);
        REQUIRE(result.submission_complete);
    }

    {
        BinaryCacheSynchronizer sync;
        sync.add_submitted();
        sync.add_submitted();
        sync.add_submitted();
        auto result = sync.fetch_add_completed();
        REQUIRE(result.jobs_submitted == 3);
        REQUIRE(result.jobs_completed == 1);
        REQUIRE_FALSE(result.submission_complete);
        REQUIRE(sync.fetch_incomplete_mark_submission_complete() == 2);
        result = sync.fetch_add_completed();
        REQUIRE(result.jobs_submitted == 2);
        REQUIRE(result.jobs_completed == 1);
        REQUIRE(result.submission_complete);
        result = sync.fetch_add_completed();
        REQUIRE(result.jobs_submitted == 2);
        REQUIRE(result.jobs_completed == 2);
        REQUIRE(result.submission_complete);
    }
}

TEST_CASE ("Test batch_command_arguments_with_fixed_length", "[batch-arguments]")
{
    static constexpr std::size_t MAX_LEN = 100;
    static constexpr std::size_t FIXED_LEN = 10;

    SECTION ("no-separator")
    {
        static constexpr StringLiteral NO_SEPARATOR = "";

        std::vector<std::string> entries;
        for (std::size_t i = 0; i < 10; ++i)
            entries.push_back(fmt::format("entryidx_{}", i));
        auto batches = batch_command_arguments_with_fixed_length(
            entries, FIXED_LEN, MAX_LEN, entries[0].length(), NO_SEPARATOR.size());

        REQUIRE(batches.size() == 2);
        REQUIRE(batches[0].size() == 9);
        REQUIRE(batches[1].size() == 1);
        CHECK(batches[0] == std::vector<std::string>{
                                "entryidx_0",
                                "entryidx_1",
                                "entryidx_2",
                                "entryidx_3",
                                "entryidx_4",
                                "entryidx_5",
                                "entryidx_6",
                                "entryidx_7",
                                "entryidx_8",
                            });
        CHECK(batches[1] == std::vector<std::string>{
                                "entryidx_9",
                            });
        auto command_len = Strings::join(NO_SEPARATOR, batches[0]).length();
        CHECK(command_len == MAX_LEN - FIXED_LEN);
        command_len = Strings::join(NO_SEPARATOR, batches[1]).length();
        CHECK(command_len < MAX_LEN - FIXED_LEN);
    }

    SECTION ("separator-and-extension")
    {
        static constexpr StringLiteral SEPARATOR = ";";
        static constexpr StringLiteral EXTENSION = ".zip";

        std::vector<std::string> entries;
        for (std::size_t i = 0; i < 10; ++i)
            entries.push_back(fmt::format("entryidx_{}", i));
        auto batches = batch_command_arguments_with_fixed_length(
            entries, FIXED_LEN, MAX_LEN, entries[0].length() + EXTENSION.size(), SEPARATOR.size());

        REQUIRE(batches.size() == 2);
        REQUIRE(batches[0].size() == 6);
        REQUIRE(batches[1].size() == 4);
        CHECK(batches[0] == std::vector<std::string>{
                                "entryidx_0",
                                "entryidx_1",
                                "entryidx_2",
                                "entryidx_3",
                                "entryidx_4",
                                "entryidx_5",
                            });
        CHECK(batches[1] == std::vector<std::string>{
                                "entryidx_6",
                                "entryidx_7",
                                "entryidx_8",
                                "entryidx_9",
                            });
        auto command_len =
            Strings::join(SEPARATOR, Util::fmap(batches[0], [](auto&& s) { return s + EXTENSION.to_string(); }))
                .length();
        CHECK(command_len < MAX_LEN - FIXED_LEN);
        command_len =
            Strings::join(SEPARATOR, Util::fmap(batches[1], [](auto&& s) { return s + EXTENSION.to_string(); }))
                .length();
        CHECK(command_len < MAX_LEN - FIXED_LEN);
    }

    SECTION ("too-long-entry")
    {
        std::vector<std::string> entries;
        for (std::size_t i = 0; i < 3; ++i)
            entries.push_back(fmt::format("entry_{}", i));
        auto batches =
            batch_command_arguments_with_fixed_length(entries, FIXED_LEN, MAX_LEN, MAX_LEN - FIXED_LEN + 1, 0);
        REQUIRE(batches.empty());
    }

    SECTION ("too-long-fixed-length")
    {
        std::vector<std::string> entries;
        for (std::size_t i = 0; i < 3; ++i)
            entries.push_back(fmt::format("entry_{}", i));
        auto batches = batch_command_arguments_with_fixed_length(entries, MAX_LEN, MAX_LEN, entries[0].length(), 0);
        REQUIRE(batches.empty());
    }

    SECTION ("empty-entries")
    {
        std::vector<std::string> entries;
        auto batches = batch_command_arguments_with_fixed_length(entries, FIXED_LEN, MAX_LEN, 1, 0);
        REQUIRE(batches.empty());
    }

    SECTION ("single-entry-fits")
    {
        std::vector<std::string> entries = {"single"};
        auto batches = batch_command_arguments_with_fixed_length(entries, FIXED_LEN, MAX_LEN, entries[0].length(), 0);
        REQUIRE(batches.size() == 1);
        REQUIRE(batches[0].size() == 1);
        CHECK(batches[0][0] == "single");
    }

    SECTION ("all entries fit in one batch")
    {
        std::vector<std::string> entries;
        for (std::size_t i = 0; i < 3; ++i)
            entries.push_back(fmt::format("entry_{}", i));
        auto batches = batch_command_arguments_with_fixed_length(entries, FIXED_LEN, MAX_LEN, entries[0].length(), 0);
        REQUIRE(batches.size() == 1);
        REQUIRE(batches[0].size() == 3);
    }
}
