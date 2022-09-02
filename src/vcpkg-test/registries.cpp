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

        std::unique_ptr<RegistryEntry> get_port_entry(StringView port_name [[maybe_unused]]) const override { return nullptr; }

        void get_all_port_names(std::vector<std::string>& port_names [[maybe_unused]]) const override { }

        ExpectedL<Version> get_baseline_version(StringView port_name [[maybe_unused]]) const override
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
        if (const auto *tri = dynamic_cast<const TestRegistryImplementation*>(&r))
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

        const auto *reg = set.registry_for_port("p1");
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

        const auto *reg = set.registry_for_port("q1");
        REQUIRE(reg);
        CHECK(get_tri_num(*reg) == 1);
        reg = set.registry_for_port("p2");
        REQUIRE(reg);
        CHECK(get_tri_num(*reg) == 2);
        reg = set.registry_for_port("a");
        CHECK_FALSE(reg);
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
