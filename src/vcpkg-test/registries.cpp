#include <catch2/catch.hpp>

#include <vcpkg/base/jsonreader.h>

#include <vcpkg/configuration.h>
#include <vcpkg/registries.h>

using namespace vcpkg;

namespace
{
    struct TestRegistryImplementation final : RegistryImplementation
    {
        StringLiteral kind() const override { return "test"; }

        std::unique_ptr<RegistryEntry> get_port_entry(StringView) const override { return nullptr; }

        void get_all_port_names(std::vector<std::string>&) const override { }

        ExpectedL<Version> get_baseline_version(StringView) const override
        {
            return LocalizedString::from_raw("error");
        }

        int number;

        TestRegistryImplementation(int n) : number(n) { }
    };

    Registry make_registry(int n, std::vector<std::string>&& port_names)
    {
        return {std::move(port_names), std::make_unique<TestRegistryImplementation>(n)};
    }

    int get_tri_num(const RegistryImplementation& r)
    {
        if (auto tri = dynamic_cast<const TestRegistryImplementation*>(&r))
        {
            return tri->number;
        }
        else
        {
            return -1;
        }
    }

    // test functions which parse string literals, so no concerns about failure
    Json::Value parse_json(StringView sv) { return Json::parse(sv).value_or_exit(VCPKG_LINE_INFO).first; }
}

TEST_CASE ("registry_set_selects_registry", "[registries]")
{
    {
        std::vector<Registry> rs;
        rs.push_back(make_registry(1, {"p1", "q1", "r1"}));
        rs.push_back(make_registry(2, {"p2", "q2", "r2"}));
        RegistrySet set(std::make_unique<TestRegistryImplementation>(0), std::move(rs));

        auto reg = set.registry_for_port("p1");
        REQUIRE(reg);
        CHECK(get_tri_num(*reg) == 1);
        reg = set.registry_for_port("r2");
        REQUIRE(reg);
        CHECK(get_tri_num(*reg) == 2);
        reg = set.registry_for_port("a");
        REQUIRE(reg);
        CHECK(get_tri_num(*reg) == 0);
    }
    {
        std::vector<Registry> rs;
        rs.push_back(make_registry(1, {"p1", "q1", "r1"}));
        rs.push_back(make_registry(2, {"p2", "q2", "r2"}));
        RegistrySet set(nullptr, std::move(rs));

        auto reg = set.registry_for_port("q1");
        REQUIRE(reg);
        CHECK(get_tri_num(*reg) == 1);
        reg = set.registry_for_port("p2");
        REQUIRE(reg);
        CHECK(get_tri_num(*reg) == 2);
        reg = set.registry_for_port("a");
        CHECK_FALSE(reg);
    }
}

TEST_CASE ("check valid package patterns", "[registries]")
{
    using PD = Json::PackagePatternDeserializer;

    CHECK(PD::is_package_pattern("co"));
    CHECK(PD::is_package_pattern("rapidjson"));
    CHECK(PD::is_package_pattern("boost-tuple"));
    CHECK(PD::is_package_pattern("vcpkg-boost-helper"));

    CHECK(PD::is_package_pattern("*"));
    CHECK(PD::is_package_pattern("b*"));
    CHECK(PD::is_package_pattern("boost*"));

    // invalid segments -
    CHECK(!PD::is_package_pattern(""));
    CHECK(!PD::is_package_pattern("-a"));
    CHECK(!PD::is_package_pattern("a-"));
    CHECK(!PD::is_package_pattern("a--"));
    CHECK(!PD::is_package_pattern("---"));

    // invalid characters
    CHECK(!PD::is_package_pattern("vcpkg@microsoft"));
    CHECK(!PD::is_package_pattern("boost#1.0.0"));
    CHECK(!PD::is_package_pattern("boost:x64-windows#1.0.0"));

    // invalid patterns
    CHECK(!PD::is_package_pattern("*a"));
    CHECK(!PD::is_package_pattern("a*a"));
    CHECK(!PD::is_package_pattern("a**"));
    CHECK(!PD::is_package_pattern("a+"));
    CHECK(!PD::is_package_pattern("a?"));
}

TEST_CASE ("calculate prefix priority", "[registries]")
{
    CHECK(package_match_prefix("boost", "*") == 1);
    CHECK(package_match_prefix("boost", "b*") == 2);
    CHECK(package_match_prefix("boost", "boost*") == 6);
    CHECK(package_match_prefix("boost", "boost") == SIZE_MAX);

    CHECK(package_match_prefix("", "") == SIZE_MAX);
    CHECK(package_match_prefix("", "*") == 1);
    CHECK(package_match_prefix("", "a") == 0);
    CHECK(package_match_prefix("boost", "") == 0);
    CHECK(package_match_prefix("boost", "c*") == 0);
    CHECK(package_match_prefix("boost", "*c") == 0);
    CHECK(package_match_prefix("boost", "c**") == 0);
    CHECK(package_match_prefix("boost", "c*a") == 0);
}

TEST_CASE ("select highest priority registry", "[registries]")
{
    std::vector<Registry> rs;
    rs.push_back(make_registry(1, {"b*"}));
    rs.push_back(make_registry(2, {"boost*"}));
    rs.push_back(make_registry(3, {"boost", "boost-tuple"}));
    rs.push_back(make_registry(4, {"boost-*"}));
    rs.push_back(make_registry(5, {"boo*"}));
    rs.push_back(make_registry(6, {"boost", "boost-tuple"}));
    RegistrySet set(std::make_unique<TestRegistryImplementation>(0), std::move(rs));

    auto reg = set.registry_for_port("boost");
    REQUIRE(reg);
    CHECK(get_tri_num(*reg) == 3);

    reg = set.registry_for_port("boost-algorithm");
    REQUIRE(reg);
    CHECK(get_tri_num(*reg) == 4);

    reg = set.registry_for_port("boost-tuple");
    REQUIRE(reg);
    CHECK(get_tri_num(*reg) == 3);

    reg = set.registry_for_port("boomerang");
    REQUIRE(reg);
    CHECK(get_tri_num(*reg) == 5);

    reg = set.registry_for_port("bang");
    REQUIRE(reg);
    CHECK(get_tri_num(*reg) == 1);

    reg = set.registry_for_port("cpprestsdk");
    REQUIRE(reg);
    CHECK(get_tri_num(*reg) == 0);
}

TEST_CASE ("sort candidate registries by priority", "[registries]")
{
    {
        std::vector<Registry> rs;
        rs.push_back(make_registry(1, {"bo*"}));
        rs.push_back(make_registry(2, {"b*"}));
        rs.push_back(make_registry(3, {"boost*"}));
        rs.push_back(make_registry(4, {"boost"}));
        RegistrySet set(nullptr, std::move(rs));

        auto candidates = set.registries_for_port("boost");
        REQUIRE(candidates.size() == 4);
        size_t idx = 0;

        auto reg = candidates[idx++];
        REQUIRE(reg);
        CHECK(get_tri_num(*reg) == 4);

        reg = candidates[idx++];
        REQUIRE(reg);
        CHECK(get_tri_num(*reg) == 3);

        reg = candidates[idx++];
        REQUIRE(reg);
        CHECK(get_tri_num(*reg) == 1);

        reg = candidates[idx++];
        REQUIRE(reg);
        CHECK(get_tri_num(*reg) == 2);
    }

    {
        std::vector<Registry> rs;
        rs.push_back(make_registry(1, {"bo*"}));
        rs.push_back(make_registry(2, {"b*"}));
        rs.push_back(make_registry(3, {"boost*"}));
        rs.push_back(make_registry(4, {"boost"}));
        RegistrySet set(nullptr, std::move(rs));

        auto candidates = set.registries_for_port("cpprestsdk");
        REQUIRE(candidates.empty());
    }
}

static vcpkg::Optional<Configuration> visit_default_registry(Json::Reader& r, Json::Value&& reg)
{
    Json::Object config;
    config.insert("default-registry", std::move(reg));
    return r.visit(config, get_configuration_deserializer());
}

TEST_CASE ("registry_parsing", "[registries]")
{
    {
        Json::Reader r;
        auto test_json = parse_json(R"json(
{
    "kind": "builtin"
}
    )json");
        visit_default_registry(r, std::move(test_json));
        CHECK(!r.errors().empty());
    }
    {
        Json::Reader r;
        auto test_json = parse_json(R"json(
{
    "kind": "builtin",
    "baseline": "hi"
}
    )json");
        visit_default_registry(r, std::move(test_json));
        // Non-SHA strings are allowed and will be diagnosed later
        CHECK(r.errors().empty());
    }
    {
        Json::Reader r;
        auto test_json = parse_json(R"json(
{
    "kind": "builtin",
    "baseline": "1234567890123456789012345678901234567890"
}
    )json");
        auto registry_impl = visit_default_registry(r, std::move(test_json));
        REQUIRE(registry_impl);
        CHECK(r.errors().empty());
    }
    {
        Json::Reader r;
        auto test_json = parse_json(R"json(
{
    "kind": "builtin",
    "baseline": "1234567890123456789012345678901234567890",
    "path": "a/b"
}
    )json");
        visit_default_registry(r, std::move(test_json));
        CHECK(!r.errors().empty());
    }
    {
        Json::Reader r;
        auto test_json = parse_json(R"json(
{
    "kind": "filesystem",
    "path": "a/b/c"
}
    )json");
        auto registry_impl = visit_default_registry(r, std::move(test_json));
        REQUIRE(registry_impl);
        CHECK(r.errors().empty());

        test_json = parse_json(R"json(
{
    "kind": "filesystem",
    "path": "/a/b/c"
}
    )json");
        registry_impl = visit_default_registry(r, std::move(test_json));
        REQUIRE(registry_impl);
        CHECK(r.errors().empty());
    }

    auto test_json = parse_json(R"json(
{
    "kind": "git"
}
    )json");
    {
        Json::Reader r;
        visit_default_registry(r, std::move(test_json));
        CHECK(!r.errors().empty());
    }
    test_json = parse_json(R"json(
{
    "kind": "git",
    "repository": "abc"
}
    )json");
    {
        Json::Reader r;
        visit_default_registry(r, std::move(test_json));
        CHECK(!r.errors().empty());
    }

    test_json = parse_json(R"json(
{
    "kind": "git",
    "baseline": "123"
}
    )json");
    {
        Json::Reader r;
        visit_default_registry(r, std::move(test_json));
        CHECK(!r.errors().empty());
    }

    test_json = parse_json(R"json(
{
    "kind": "git",
    "repository": "abc",
    "baseline": "123",
    "reference": "abc/def"
}
    )json");
    {
        Json::Reader r;
        auto registry_impl = visit_default_registry(r, std::move(test_json));
        REQUIRE(registry_impl);
        INFO(Strings::join("\n", r.errors()));
        CHECK(r.errors().empty());
    }

    test_json = parse_json(R"json(
{
    "kind": "git",
    "repository": "abc",
    "baseline": "123"
}
    )json");
    Json::Reader r;
    auto registry_impl = visit_default_registry(r, std::move(test_json));
    REQUIRE(registry_impl);
    INFO(Strings::join("\n", r.errors()));
    CHECK(r.errors().empty());
}

TEST_CASE ("registries ignored patterns warning", "[registries]")
{
    auto test_json = parse_json(R"json({
    "registries": [
        {
            "kind": "git",
            "repository": "0",
            "baseline": "ffff0000",
            "packages": [ "beicode", "beison", "bei*" ]
        },
        {
            "kind": "git",
            "repository": "1",
            "baseline": "aaaa0000",
            "packages": [ "beicode", "bei*", "fmt" ]
        },
        {
            "kind": "git",
            "repository": "2",
            "baseline": "bbbb0000",
            "packages": [ "beison", "fmt", "*" ]
        }
    ]
})json");

    Json::Reader r;
    auto maybe_conf = r.visit(test_json, get_configuration_deserializer());

    auto conf = maybe_conf.get();
    REQUIRE(conf);

    auto& regs = conf->registries;
    REQUIRE(regs.size() == 3);

    auto reg = regs[0];
    reg.kind = "git";
    reg.repo = "0";
    reg.baseline = "ffff0000";
    auto pkgs = reg.packages.get();
    REQUIRE(pkgs);
    CHECK((*pkgs)[0] == "beicode");
    CHECK((*pkgs)[1] == "beison");
    CHECK((*pkgs)[2] == "bei*");

    reg = regs[1];
    reg.kind = "git";
    reg.repo = "1";
    reg.baseline = "aaaa0000";
    pkgs = reg.packages.get();
    REQUIRE(pkgs);
    CHECK((*pkgs)[0] == "beicode");
    CHECK((*pkgs)[1] == "bei*");
    CHECK((*pkgs)[2] == "fmt");

    reg = regs[2];
    reg.kind = "git";
    reg.repo = "2";
    reg.baseline = "bbbb0000";
    pkgs = reg.packages.get();
    REQUIRE(pkgs);
    CHECK((*pkgs)[0] == "beison");
    CHECK((*pkgs)[1] == "fmt");
    CHECK((*pkgs)[2] == "*");

    const auto& warnings = r.warnings();
    CHECK(warnings.size() == 4);
    CHECK(warnings[0] ==
          "$.registries[1].packages[0] (a package pattern): Package \"beicode\" is already declared by another "
          "registry.\n"
          "\tDuplicate entries will be ignored.\n\tRemove any duplicate entries to dismiss this warning.");
    CHECK(warnings[1] ==
          "$.registries[1].packages[1] (a package pattern): Pattern \"bei*\" is already declared by another registry.\n"
          "\tDuplicate entries will be ignored.\n\tRemove any duplicate entries to dismiss this warning.");
    CHECK(
        warnings[2] ==
        "$.registries[2].packages[0] (a package pattern): Package \"beison\" is already declared by another registry.\n"
        "\tDuplicate entries will be ignored.\n\tRemove any duplicate entries to dismiss this warning.");
    CHECK(warnings[3] ==
          "$.registries[2].packages[1] (a package pattern): Package \"fmt\" is already declared by another registry.\n"
          "\tDuplicate entries will be ignored.\n\tRemove any duplicate entries to dismiss this warning.");
}

TEST_CASE ("git_version_db_parsing", "[registries]")
{
    VersionDbEntryArrayDeserializer filesystem_version_db{VersionDbType::Git, "a/b"};
    Json::Reader r;
    auto test_json = parse_json(R"json(
[
    {
        "git-tree": "9b07f8a38bbc4d13f8411921e6734753e15f8d50",
        "version-date": "2021-06-26",
        "port-version": 0
    },
    {
        "git-tree": "12b84a31469a78dd4b42dcf58a27d4600f6b2d48",
        "version-date": "2021-01-14",
        "port-version": 0
    },
    {
        "git-tree": "bd4565e8ab55bc5e098a1750fa5ff0bc4406ca9b",
        "version-string": "2020-04-12",
        "port-version": 0
    }
]
)json");

    auto results_opt = r.visit(test_json, filesystem_version_db);
    auto& results = results_opt.value_or_exit(VCPKG_LINE_INFO);
    CHECK(results[0].version == Version{"2021-06-26", 0});
    CHECK(results[0].git_tree == "9b07f8a38bbc4d13f8411921e6734753e15f8d50");
    CHECK(results[1].version == Version{"2021-01-14", 0});
    CHECK(results[1].git_tree == "12b84a31469a78dd4b42dcf58a27d4600f6b2d48");
    CHECK(results[2].version == Version{"2020-04-12", 0});
    CHECK(results[2].git_tree == "bd4565e8ab55bc5e098a1750fa5ff0bc4406ca9b");
    CHECK(r.errors().empty());
}

TEST_CASE ("filesystem_version_db_parsing", "[registries]")
{
    VersionDbEntryArrayDeserializer filesystem_version_db{VersionDbType::Filesystem, "a/b"};

    {
        Json::Reader r;
        auto test_json = parse_json(R"json(
[
    {
        "version-string": "puppies",
        "port-version": 0,
        "path": "$/c/d"
    },
    {
        "version-string": "doggies",
        "port-version": 0,
        "path": "$/e/d"
    },
    {
        "version-semver": "1.2.3",
        "port-version": 0,
        "path": "$/semvers/here"
    }
]
    )json");
        auto results_opt = r.visit(test_json, filesystem_version_db);
        auto& results = results_opt.value_or_exit(VCPKG_LINE_INFO);
        CHECK(results[0].version == Version{"puppies", 0});
        CHECK(results[0].p == "a/b" VCPKG_PREFERRED_SEPARATOR "c/d");
        CHECK(results[1].version == Version{"doggies", 0});
        CHECK(results[1].p == "a/b" VCPKG_PREFERRED_SEPARATOR "e/d");
        CHECK(results[2].version == Version{"1.2.3", 0});
        CHECK(results[2].p == "a/b" VCPKG_PREFERRED_SEPARATOR "semvers/here");
        CHECK(r.errors().empty());
    }

    { // missing $/
        Json::Reader r;
        auto test_json = parse_json(R"json(
[
    {
        "version-string": "puppies",
        "port-version": 0,
        "path": "c/d"
    }
]
    )json");
        CHECK(r.visit(test_json, filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
        CHECK(!r.errors().empty());
    }

    { // uses backslash
        Json::Reader r;
        auto test_json = parse_json(R"json(
[
    {
        "version-string": "puppies",
        "port-version": 0,
        "path": "$\\c\\d"
    }
]
    )json");
        CHECK(r.visit(test_json, filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
        CHECK(!r.errors().empty());
    }

    { // doubled slash
        Json::Reader r;
        auto test_json = parse_json(R"json(
[
    {
        "version-string": "puppies",
        "port-version": 0,
        "path": "$/c//d"
    }
]
    )json");
        CHECK(r.visit(test_json, filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
        CHECK(!r.errors().empty());
    }

    { // dot path (first)
        Json::Reader r;
        auto test_json = parse_json(R"json(
[
    {
        "version-string": "puppies",
        "port-version": 0,
        "path": "$/./d/a/a"
    }
]
    )json");
        CHECK(r.visit(test_json, filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
        CHECK(!r.errors().empty());
    }

    { // dot path (mid)
        Json::Reader r;
        auto test_json = parse_json(R"json(
[
    {
        "version-string": "puppies",
        "port-version": 0,
        "path": "$/c/d/./a"
    }
]
    )json");
        CHECK(r.visit(test_json, filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
        CHECK(!r.errors().empty());
    }

    { // dot path (last)
        Json::Reader r;
        auto test_json = parse_json(R"json(
[
    {
        "version-string": "puppies",
        "port-version": 0,
        "path": "$/c/d/."
    }
]
    )json");
        CHECK(r.visit(test_json, filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
        CHECK(!r.errors().empty());
    }

    { // dot dot path (first)
        Json::Reader r;
        auto test_json = parse_json(R"json(
[
    {
        "version-string": "puppies",
        "port-version": 0,
        "path": "$/../d/a/a"
    }
]
    )json");
        CHECK(r.visit(test_json, filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
        CHECK(!r.errors().empty());
    }

    { // dot dot path (mid)
        Json::Reader r;
        auto test_json = parse_json(R"json(
[
    {
        "version-string": "puppies",
        "port-version": 0,
        "path": "$/c/d/../a"
    }
]
    )json");
        CHECK(r.visit(test_json, filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
        CHECK(!r.errors().empty());
    }

    { // dot dot path (last)
        Json::Reader r;
        auto test_json = parse_json(R"json(
[
    {
        "version-string": "puppies",
        "port-version": 0,
        "path": "$/c/d/.."
    }
]
    )json");
        CHECK(r.visit(test_json, filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
        CHECK(!r.errors().empty());
    }
}
