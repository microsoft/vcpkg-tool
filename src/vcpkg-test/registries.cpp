#include <catch2/catch.hpp>

#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/configuration.h>
#include <vcpkg/registries.h>
#include <vcpkg/registries.private.h>

using namespace vcpkg;

namespace
{
    struct TestRegistryImplementation final : RegistryImplementation
    {
        StringLiteral kind() const override { return "test"; }

        std::unique_ptr<RegistryEntry> get_port_entry(StringView) const override { return nullptr; }

        void append_all_port_names(std::vector<std::string>&) const override { }

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
    Json::Value parse_json(StringView sv) { return Json::parse(sv).value(VCPKG_LINE_INFO).value; }
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
    using ID = Json::IdentifierDeserializer;

    // test identifiers
    CHECK(ID::is_ident("co"));
    CHECK(ID::is_ident("rapidjson"));
    CHECK(ID::is_ident("boost-tuple"));
    CHECK(ID::is_ident("vcpkg-boost-helper"));

    // reject invalid characters
    CHECK(!ID::is_ident(""));
    CHECK(!ID::is_ident("boost_tuple"));
    CHECK(!ID::is_ident("boost.tuple"));
    CHECK(!ID::is_ident("boost@1"));
    CHECK(!ID::is_ident("boost#1"));
    CHECK(!ID::is_ident("boost:x64-windows"));

    // accept legacy
    CHECK(ID::is_ident("all_modules"));

    // reject reserved keywords
    CHECK(!ID::is_ident("prn"));
    CHECK(!ID::is_ident("aux"));
    CHECK(!ID::is_ident("nul"));
    CHECK(!ID::is_ident("con"));
    CHECK(!ID::is_ident("core"));
    CHECK(!ID::is_ident("default"));
    CHECK(!ID::is_ident("lpt1"));
    CHECK(!ID::is_ident("com1"));

    // reject incomplete segments
    CHECK(!ID::is_ident("-a"));
    CHECK(!ID::is_ident("a-"));
    CHECK(!ID::is_ident("a--"));
    CHECK(!ID::is_ident("---"));

    // accept prefixes
    CHECK(is_package_pattern("*"));
    CHECK(is_package_pattern("b*"));
    CHECK(is_package_pattern("boost*"));
    CHECK(is_package_pattern("boost-*"));

    // reject invalid patterns
    CHECK(!is_package_pattern("*a"));
    CHECK(!is_package_pattern("a*a"));
    CHECK(!is_package_pattern("a**"));
    CHECK(!is_package_pattern("a+"));
    CHECK(!is_package_pattern("a?"));
}

TEST_CASE ("calculate prefix priority", "[registries]")
{
    CHECK(package_pattern_match("boost", "*") == 1);
    CHECK(package_pattern_match("boost", "b*") == 2);
    CHECK(package_pattern_match("boost", "boost*") == 6);
    CHECK(package_pattern_match("boost", "boost") == SIZE_MAX);

    CHECK(package_pattern_match("", "") == SIZE_MAX);
    CHECK(package_pattern_match("", "*") == 1);
    CHECK(package_pattern_match("", "a") == 0);
    CHECK(package_pattern_match("boost", "") == 0);
    CHECK(package_pattern_match("boost", "c*") == 0);
    CHECK(package_pattern_match("boost", "*c") == 0);
    CHECK(package_pattern_match("boost", "c**") == 0);
    CHECK(package_pattern_match("boost", "c*a") == 0);
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

TEST_CASE ("registries report pattern errors", "[registries]")
{
    auto test_json = parse_json(R"json({
    "registries": [
        {
            "kind": "git",
            "repository": "https://github.com/Microsoft/vcpkg",
            "baseline": "ffff0000",
            "packages": [ "*", "", "a*a", "*a" ]
        }
    ]
})json");

    Json::Reader r;
    auto maybe_conf = r.visit(test_json, get_configuration_deserializer());
    const auto& errors = r.errors();
    CHECK(!errors.empty());
    REQUIRE(errors.size() == 3);
    CHECK(errors[0] ==
          "$.registries[0].packages[1] (a package pattern): \"\" is not a valid package pattern. Package patterns must "
          "use only one wildcard character (*) and it must be the last character in the pattern (see "
          "https://learn.microsoft.com/vcpkg/users/registries for more information)");
    CHECK(errors[1] ==
          "$.registries[0].packages[2] (a package pattern): \"a*a\" is not a valid package pattern. Package patterns "
          "must use only one wildcard character (*) and it must be the last character in the pattern (see "
          "https://learn.microsoft.com/vcpkg/users/registries for more information)");
    CHECK(errors[2] ==
          "$.registries[0].packages[3] (a package pattern): \"*a\" is not a valid package pattern. Package patterns "
          "must use only one wildcard character (*) and it must be the last character in the pattern (see "
          "https://learn.microsoft.com/vcpkg/users/registries for more information)");
}

TEST_CASE ("registries ignored patterns warning", "[registries]")
{
    auto test_json = parse_json(R"json({
    "registries": [
        {
            "kind": "git",
            "repository": "https://github.com/Microsoft/vcpkg",
            "baseline": "ffff0000",
            "packages": [ "*", "rapidjson", "zlib" ]
        },
        {
            "kind": "git",
            "repository": "https://github.com/northwindtraders/vcpkg-registry",
            "baseline": "aaaa0000",
            "packages": [ "bei*", "zlib" ]
        },
        {
            "kind": "git",
            "repository": "https://github.com/another-remote/another-vcpkg-registry",
            "baseline": "bbbb0000",
            "packages": [ "*", "bei*", "zlib" ]
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
    CHECK(reg.kind == "git");
    CHECK(reg.repo == "https://github.com/Microsoft/vcpkg");
    CHECK(reg.baseline == "ffff0000");
    auto pkgs = reg.packages.get();
    REQUIRE(pkgs);
    REQUIRE(pkgs->size() == 3);
    CHECK((*pkgs)[0] == "*");
    CHECK((*pkgs)[1] == "rapidjson");
    CHECK((*pkgs)[2] == "zlib");

    reg = regs[1];
    CHECK(reg.kind == "git");
    CHECK(reg.repo == "https://github.com/northwindtraders/vcpkg-registry");
    CHECK(reg.baseline == "aaaa0000");
    pkgs = reg.packages.get();
    REQUIRE(pkgs);
    REQUIRE(pkgs->size() == 2);
    CHECK((*pkgs)[0] == "bei*");
    CHECK((*pkgs)[1] == "zlib");

    reg = regs[2];
    CHECK(reg.kind == "git");
    CHECK(reg.repo == "https://github.com/another-remote/another-vcpkg-registry");
    CHECK(reg.baseline == "bbbb0000");
    pkgs = reg.packages.get();
    REQUIRE(pkgs);
    REQUIRE(pkgs->size() == 3);
    CHECK((*pkgs)[0] == "*");
    CHECK((*pkgs)[1] == "bei*");
    CHECK((*pkgs)[2] == "zlib");

    const auto& warnings = r.warnings();
    REQUIRE(warnings.size() == 3);
    CHECK(warnings[0] == R"($ (a configuration object): warning: Package "*" is duplicated.
    First declared in:
        location: $.registries[0].packages[0]
        registry: https://github.com/Microsoft/vcpkg

    The following redeclarations will be ignored:
        location: $.registries[2].packages[0]
        registry: https://github.com/another-remote/another-vcpkg-registry
)");
    CHECK(warnings[1] == R"($ (a configuration object): warning: Package "bei*" is duplicated.
    First declared in:
        location: $.registries[1].packages[0]
        registry: https://github.com/northwindtraders/vcpkg-registry

    The following redeclarations will be ignored:
        location: $.registries[2].packages[1]
        registry: https://github.com/another-remote/another-vcpkg-registry
)");
    CHECK(warnings[2] == R"($ (a configuration object): warning: Package "zlib" is duplicated.
    First declared in:
        location: $.registries[0].packages[2]
        registry: https://github.com/Microsoft/vcpkg

    The following redeclarations will be ignored:
        location: $.registries[1].packages[1]
        registry: https://github.com/northwindtraders/vcpkg-registry

        location: $.registries[2].packages[2]
        registry: https://github.com/another-remote/another-vcpkg-registry
)");
}

TEST_CASE ("git_version_db_parsing", "[registries]")
{
    auto filesystem_version_db = make_version_db_deserializer(VersionDbType::Git, "a/b");
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

    auto results_opt = r.visit(test_json, *filesystem_version_db);
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
    auto filesystem_version_db = make_version_db_deserializer(VersionDbType::Filesystem, "a/b");

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
        auto results_opt = r.visit(test_json, *filesystem_version_db);
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
        CHECK(r.visit(test_json, *filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
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
        CHECK(r.visit(test_json, *filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
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
        CHECK(r.visit(test_json, *filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
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
        CHECK(r.visit(test_json, *filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
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
        CHECK(r.visit(test_json, *filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
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
        CHECK(r.visit(test_json, *filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
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
        CHECK(r.visit(test_json, *filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
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
        CHECK(r.visit(test_json, *filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
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
        CHECK(r.visit(test_json, *filesystem_version_db).value_or_exit(VCPKG_LINE_INFO).empty());
        CHECK(!r.errors().empty());
    }
}
