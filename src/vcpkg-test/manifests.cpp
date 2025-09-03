#include <vcpkg-test/util.h>

#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/json.h>

#include <vcpkg/documentation.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;
using namespace vcpkg::Test;

static Json::Object parse_json_object(StringView sv)
{
    auto json = Json::parse(sv, "test");
    // we're not testing json parsing here, so just fail on errors
    if (auto r = json.get())
    {
        return std::move(r->value.object(VCPKG_LINE_INFO));
    }
    else
    {
        INFO("Error found while parsing JSON document:");
        INFO(sv.to_string());
        FAIL(json.error());
        return Json::Object{};
    }
}

enum class PrintErrors : bool
{
    No,
    Yes,
};

static ExpectedL<std::unique_ptr<SourceControlFile>> test_parse_project_manifest(const Json::Object& obj,
                                                                                 PrintErrors print = PrintErrors::Yes)
{
    auto res = SourceControlFile::parse_project_manifest_object("<test manifest>", obj, null_sink);
    if (!res.has_value() && print == PrintErrors::Yes)
    {
        msg::println(Color::error, res.error());
    }
    return res;
}

static ExpectedL<std::unique_ptr<SourceControlFile>> test_parse_port_manifest(const Json::Object& obj,
                                                                              PrintErrors print = PrintErrors::Yes)
{
    auto res = SourceControlFile::parse_port_manifest_object("<test manifest>", obj, null_sink);
    if (!res.has_value() && print == PrintErrors::Yes)
    {
        msg::println(Color::error, res.error());
    }
    return res;
}

static ExpectedL<std::unique_ptr<SourceControlFile>> test_parse_project_manifest(StringView obj,
                                                                                 PrintErrors print = PrintErrors::Yes)
{
    return test_parse_project_manifest(parse_json_object(obj), print);
}
static ExpectedL<std::unique_ptr<SourceControlFile>> test_parse_port_manifest(StringView obj,
                                                                              PrintErrors print = PrintErrors::Yes)
{
    return test_parse_port_manifest(parse_json_object(obj), print);
}

static bool project_manifest_is_parsable(StringView obj)
{
    return test_parse_project_manifest(parse_json_object(obj), PrintErrors::No).has_value();
}

static bool port_manifest_is_parsable(const Json::Object& obj)
{
    return test_parse_port_manifest(obj, PrintErrors::No).has_value();
}
static bool port_manifest_is_parsable(StringView obj)
{
    return test_parse_port_manifest(parse_json_object(obj), PrintErrors::No).has_value();
}

static const FeatureFlagSettings feature_flags_with_versioning{false, false, false, true};
static const FeatureFlagSettings feature_flags_without_versioning{false, false, false, false};

TEST_CASE ("manifest construct minimum", "[manifests]")
{
    auto m_pgh = test_parse_port_manifest(R"json({
        "name": "zlib",
        "version-string": "1.2.8"
    })json");

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.to_name() == "zlib");
    REQUIRE(pgh.to_version_scheme() == VersionScheme::String);
    REQUIRE(pgh.to_version() == Version{"1.2.8", 0});
    REQUIRE(pgh.core_paragraph->maintainers.empty());
    REQUIRE(pgh.core_paragraph->contacts.is_empty());
    REQUIRE(pgh.core_paragraph->summary.empty());
    REQUIRE(pgh.core_paragraph->description.empty());
    REQUIRE(pgh.core_paragraph->dependencies.empty());
    REQUIRE(!pgh.core_paragraph->builtin_baseline.has_value());
    REQUIRE(!pgh.core_paragraph->configuration.has_value());

    REQUIRE(pgh.check_against_feature_flags({}, feature_flags_without_versioning));

    // name must be present:
    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "",
        "version-string": "abcd"
    })json"));

    // name can't be empty:
    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "",
        "version-string": "abcd"
    })json"));
}

TEST_CASE ("project manifest construct minimum", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({})json");

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.to_name().empty());
    REQUIRE(pgh.to_version_scheme() == VersionScheme::Missing);
    REQUIRE(pgh.to_version() == Version{});
    REQUIRE(pgh.core_paragraph->maintainers.empty());
    REQUIRE(pgh.core_paragraph->contacts.is_empty());
    REQUIRE(pgh.core_paragraph->summary.empty());
    REQUIRE(pgh.core_paragraph->description.empty());
    REQUIRE(pgh.core_paragraph->dependencies.empty());
    REQUIRE(!pgh.core_paragraph->builtin_baseline.has_value());
    REQUIRE(!pgh.core_paragraph->configuration.has_value());

    // name might not be present:
    REQUIRE(project_manifest_is_parsable(R"json({
        "version-string": "abcd"
    })json"));

    // name can't be empty if supplied:
    REQUIRE_FALSE(project_manifest_is_parsable(R"json({
        "name": "",
        "version-string": "abcd"
    })json"));

    REQUIRE(project_manifest_is_parsable(R"json({
        "name": "some-name",
        "version-string": "abcd"
    })json"));
}

TEST_CASE ("manifest versioning", "[manifests]")
{
    std::tuple<StringLiteral, VersionScheme, StringLiteral> data[] = {
        {R"json({
    "name": "zlib",
    "version-string": "abcd"
}
)json",
         VersionScheme::String,
         "abcd"},
        {R"json({
    "name": "zlib",
    "version-date": "2020-01-01"
}
)json",
         VersionScheme::Date,
         "2020-01-01"},
        {R"json({
    "name": "zlib",
    "version": "1.2.3.4.5"
}
)json",
         VersionScheme::Relaxed,
         "1.2.3.4.5"},
        {R"json({
    "name": "zlib",
    "version-semver": "1.2.3-rc3"
}
)json",
         VersionScheme::Semver,
         "1.2.3-rc3"},
    };
    for (auto&& v : data)
    {
        auto portManifest = Json::parse_object(std::get<0>(v), "test").value_or_exit(VCPKG_LINE_INFO);
        { // project manifest
            auto projectManifest = portManifest;
            projectManifest.remove("name");
            auto m_pgh = test_parse_project_manifest(projectManifest);
            REQUIRE(m_pgh.has_value());
            auto& pgh = **m_pgh.get();
            CHECK(Json::stringify(serialize_manifest(pgh)) == Json::stringify(projectManifest));
            CHECK(pgh.core_paragraph->version_scheme == std::get<1>(v));
            CHECK(pgh.core_paragraph->version == Version{std::get<2>(v), 0});
        }

        { // port manifest
            auto m_pgh = test_parse_port_manifest(portManifest);
            REQUIRE(m_pgh.has_value());
            auto& pgh = **m_pgh.get();
            CHECK(Json::stringify(serialize_manifest(pgh)) == Json::stringify(portManifest));
            CHECK(pgh.core_paragraph->version_scheme == std::get<1>(v));
            CHECK(pgh.core_paragraph->version == Version{std::get<2>(v), 0});
        }
    }

    REQUIRE_FALSE(project_manifest_is_parsable(R"json({
        "version-string": "abcd",
        "version-semver": "1.2.3-rc3"
    })json"));

    REQUIRE_FALSE(project_manifest_is_parsable(R"json({
        "name": "zlib",
        "version-string": "abcd",
        "version-semver": "1.2.3-rc3"
    })json"));

    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "zlib",
        "version-string": "abcd",
        "version-semver": "1.2.3-rc3"
    })json"));

    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "zlib",
        "version-string": "abcd#1"
    })json"));
    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "zlib",
        "version": "abcd#1"
    })json"));
    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "zlib",
        "version-date": "abcd#1"
    })json"));
    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "zlib",
        "version-semver": "abcd#1"
    })json"));

    SECTION ("version syntax")
    {
        REQUIRE_FALSE(project_manifest_is_parsable(R"json({
        "name": "zlib",
        "version-semver": "2020-01-01"
    })json"));
        REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "zlib",
        "version-semver": "2020-01-01"
    })json"));
        REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "zlib",
        "version-date": "1.1.1"
    })json"));
        REQUIRE(port_manifest_is_parsable(R"json({
        "name": "zlib",
        "version": "1.2.3-rc3"
    })json"));
    }
}

TEST_CASE ("manifest constraints hash", "[manifests]")
{
    {
        auto m_pgh = test_parse_port_manifest(R"json({
            "name": "zlib",
            "version-string": "abcd",
            "dependencies": [
                {
                    "name": "d",
                    "version>=": "2018-09-01#1"
                }
            ]
        })json");
        REQUIRE(m_pgh.has_value());
        const auto& p = *m_pgh.get();
        REQUIRE(p->core_paragraph->dependencies.at(0).constraint.version == Version{"2018-09-01", 1});
    }

    {
        auto m_pgh = test_parse_port_manifest(R"json({
            "name": "zlib",
            "version-string": "abcd",
            "dependencies": [
                {
                    "name": "d",
                    "version>=": "2018-09-01#0"
                }
            ]
        })json");
        REQUIRE(m_pgh.has_value());
        const auto& p = *m_pgh.get();
        REQUIRE(p->core_paragraph->dependencies.at(0).constraint.version == Version{"2018-09-01", 0});
    }

    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "dependencies": [
        {
            "name": "d",
            "version>=": "2018-09-01#-1"
        }
    ]
})json"));

    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "dependencies": [
        {
            "name": "d",
            "version>=": "2018-09-01",
            "port-version": 1
        }
    ]
})json"));
}

TEST_CASE ("manifest overrides embedded port version", "[manifests]")
{
    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-string": "abcd#1",
            "port-version": 1
        }
    ]
})json"));
    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-date": "2018-01-01#1",
            "port-version": 1
        }
    ]
})json"));
    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version": "1.2#1",
            "port-version": 1
        }
    ]
})json"));
    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-semver": "1.2.0#1",
            "port-version": 1
        }
    ]
})json"));

    auto parsed = test_parse_port_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-string": "abcd#1"
        }
    ]
})json");
    REQUIRE(parsed.has_value());
    {
        const auto& first_override = (*parsed.get())->core_paragraph->overrides.at(0);
        CHECK(first_override.version == Version{"abcd", 1});
    }

    parsed = test_parse_port_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-date": "2018-01-01#1"
        }
    ]
})json");
    REQUIRE(parsed.has_value());
    {
        const auto& first_override = (*parsed.get())->core_paragraph->overrides.at(0);
        CHECK(first_override.version == Version{"2018-01-01", 1});
    }

    parsed = test_parse_port_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version": "1.2#1"
        }
    ]
})json");
    REQUIRE(parsed.has_value());
    {
        const auto& first_override = (*parsed.get())->core_paragraph->overrides.at(0);
        CHECK(first_override.version == Version{"1.2", 1});
    }

    parsed = test_parse_port_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-semver": "1.2.0#1"
        }
    ]
})json");
    REQUIRE(parsed.has_value());
    {
        const auto& first_override = (*parsed.get())->core_paragraph->overrides.at(0);
        CHECK(first_override.version == Version{"1.2.0", 1});
    }
}

TEST_CASE ("manifest constraints", "[manifests]")
{
    std::string raw = R"json({
    "name": "zlib",
    "version-string": "abcd",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "dependencies": [
        "a",
        {
            "$extra": null,
            "name": "c"
        },
        {
            "name": "d",
            "version>=": "2018-09-01"
        }
    ]
}
)json";
    auto m_pgh = test_parse_port_manifest(raw);

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();
    REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
    REQUIRE(pgh.check_against_feature_flags({}, feature_flags_with_versioning));
    REQUIRE(Json::stringify(serialize_manifest(pgh), Json::JsonStyle::with_spaces(4)) == raw);
    REQUIRE(pgh.core_paragraph->dependencies.size() == 3);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "a");
    REQUIRE(pgh.core_paragraph->dependencies[0].constraint == DependencyConstraint{});
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "c");
    REQUIRE(pgh.core_paragraph->dependencies[1].constraint == DependencyConstraint{});
    REQUIRE(pgh.core_paragraph->dependencies[2].name == "d");
    REQUIRE(pgh.core_paragraph->dependencies[2].constraint ==
            DependencyConstraint{VersionConstraintKind::Minimum, Version{"2018-09-01", 0}});
    REQUIRE(pgh.core_paragraph->builtin_baseline == "089fa4de7dca22c67dcab631f618d5cd0697c8d4");

    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "zlib",
        "version-string": "abcd",
        "dependencies": [
            {
                "name": "d",
                "port-version": 5
            }
        ]
    })json"));
}

TEST_CASE ("manifest builtin-baseline", "[manifests]")
{
    SECTION ("valid baseline")
    {
        std::string raw = R"json({
    "name": "zlib",
    "version-string": "abcd",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4"
}
)json";
        auto m_pgh = test_parse_port_manifest(raw);

        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_with_versioning));
        REQUIRE(pgh.core_paragraph->builtin_baseline.value_or("does not have a value") ==
                "089fa4de7dca22c67dcab631f618d5cd0697c8d4");
    }

    SECTION ("valid CAPITAL baseline")
    {
        std::string raw = R"json({
    "name": "zlib",
    "version-string": "abcd",
    "builtin-baseline": "089FA4DE7DCA22C67DCAB631F618D5CD0697C8D4"
}
)json";
        auto m_pgh = test_parse_port_manifest(raw);

        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_with_versioning));
        REQUIRE(pgh.core_paragraph->builtin_baseline.value_or("does not have a value") ==
                "089FA4DE7DCA22C67DCAB631F618D5CD0697C8D4");
    }

    SECTION ("empty baseline")
    {
        std::string raw = R"json({
    "name": "zlib",
    "version-string": "abcd",
    "builtin-baseline": ""
}
)json";

        auto m_pgh = test_parse_port_manifest(raw);

        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_with_versioning));
        REQUIRE(pgh.core_paragraph->builtin_baseline.value_or("does not have a value") == "");
    }

    SECTION ("valid required baseline")
    {
        std::string raw = R"json({
    "name": "zlib",
    "version-string": "abcd",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "dependencies": [
        {
            "name": "abc",
            "version>=": "abcd#1"
        }
    ],
    "overrides": [
        {
            "name": "abc",
            "version-string": "abcd"
        }
    ]
}
)json";

        auto m_pgh = test_parse_port_manifest(raw);
        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(pgh.core_paragraph->dependencies.size() == 1);
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.type == VersionConstraintKind::Minimum);
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.version == Version{"abcd", 1});
        REQUIRE(pgh.core_paragraph->overrides.size() == 1);
        const auto& first_override = pgh.core_paragraph->overrides[0];
        REQUIRE(first_override.version == Version{"abcd", 0});
        REQUIRE(pgh.core_paragraph->builtin_baseline.value_or("does not have a value") ==
                "089fa4de7dca22c67dcab631f618d5cd0697c8d4");
        REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_with_versioning));
    }

    SECTION ("missing required baseline")
    {
        std::string raw = R"json({
    "name": "zlib",
    "version-string": "abcd",
    "dependencies": [
        {
            "name": "abc",
            "version>=": "abcd#1"
        }
    ],
    "overrides": [
        {
            "name": "abc",
            "version-string": "abcd"
        }
    ]
}
)json";

        auto m_pgh = test_parse_port_manifest(raw);
        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(pgh.core_paragraph->dependencies.size() == 1);
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.type == VersionConstraintKind::Minimum);
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.version == Version{"abcd", 1});
        REQUIRE(pgh.core_paragraph->overrides.size() == 1);
        const auto& first_override = pgh.core_paragraph->overrides[0];
        REQUIRE(first_override.version == Version{"abcd", 0});
        REQUIRE(!pgh.core_paragraph->builtin_baseline.has_value());
        REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_with_versioning));
    }
}

struct ManifestTestCase
{
    StringLiteral input;
    StringLiteral reserialized;
    StringLiteral override_version_text;
};

TEST_CASE ("manifest overrides", "[manifests]")
{
    static constexpr ManifestTestCase data[] = {
        {R"json({
    "name": "zlib",
    "version-date": "2020-01-01",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version-string": "abcd"
        }
    ]
}
)json",
         R"json({
    "name": "zlib",
    "version-date": "2020-01-01",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version": "abcd"
        }
    ]
}
)json",
         "abcd"},
        {R"json({
    "name": "zlib",
    "version": "1.2.3.4.5",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version-date": "2020-01-01"
        }
    ]
}
)json",
         R"json({
    "name": "zlib",
    "version": "1.2.3.4.5",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version": "2020-01-01"
        }
    ]
}
)json",
         "2020-01-01"},
        {R"json({
    "name": "zlib",
    "version-date": "2020-01-01",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version": "1.2.3.4.5"
        }
    ]
}
)json",
         R"json({
    "name": "zlib",
    "version-date": "2020-01-01",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version": "1.2.3.4.5"
        }
    ]
}
)json",
         "1.2.3.4.5"},
        {R"json({
    "name": "zlib",
    "version-date": "2020-01-01",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version-semver": "1.2.3-rc3"
        }
    ]
}
)json",
         R"json({
    "name": "zlib",
    "version-date": "2020-01-01",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version": "1.2.3-rc3"
        }
    ]
}
)json",
         "1.2.3-rc3"},
        {R"json({
    "name": "zlib",
    "version-date": "2020-01-01",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version": "not.a.valid.relaxed.version"
        }
    ]
}
)json",
         R"json({
    "name": "zlib",
    "version-date": "2020-01-01",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version": "not.a.valid.relaxed.version"
        }
    ]
}
)json",
         "not.a.valid.relaxed.version"},
    };
    for (auto&& v : data)
    {
        auto m_pgh = test_parse_port_manifest(v.input);

        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(Json::stringify(serialize_manifest(pgh), Json::JsonStyle::with_spaces(4)) == v.reserialized);
        REQUIRE(pgh.core_paragraph->overrides.size() == 1);
        const auto& first_override = pgh.core_paragraph->overrides[0];
        REQUIRE(first_override.version == Version{v.override_version_text, 0});
        REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_with_versioning));
    }

    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "zlib",
        "version-string": "abcd",
        "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
        "overrides": [
        {
            "name": "abc",
            "version-semver": "1.2.3-rc3",
            "version-string": "1.2.3-rc3"
        }
    ]})json"));

    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "zlib",
        "version-string": "abcd",
        "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
        "overrides": [
        {
            "name": "abc",
            "port-version": 5
        }
    ]})json"));

    std::string raw = R"json({
    "name": "zlib",
    "version-string": "abcd",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version-string": "hello",
            "port-version": 5
        },
        {
            "name": "abcd",
            "version-string": "hello",
            "port-version": 7
        }
    ]
}
)json";
    auto m_pgh = test_parse_port_manifest(raw);

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();
    REQUIRE(Json::stringify(serialize_manifest(pgh), Json::JsonStyle::with_spaces(4)) == R"json({
    "name": "zlib",
    "version-string": "abcd",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "overrides": [
        {
            "name": "abc",
            "version": "hello#5"
        },
        {
            "name": "abcd",
            "version": "hello#7"
        }
    ]
}
)json");
    REQUIRE(pgh.core_paragraph->overrides.size() == 2);
    {
        const auto& first_override = pgh.core_paragraph->overrides[0];
        const auto& second_override = pgh.core_paragraph->overrides[1];
        REQUIRE(first_override.name == "abc");
        REQUIRE(first_override.version == Version{"hello", 5});
        REQUIRE(second_override.name == "abcd");
        REQUIRE(second_override.version == Version{"hello", 7});
    }

    REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
    REQUIRE(pgh.check_against_feature_flags({}, feature_flags_with_versioning));
}

TEST_CASE ("manifest embed configuration", "[manifests]")
{
    std::string raw_config = R"json({
        "$extra-info": null,
        "default-registry": {
            "kind": "builtin",
            "baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4"
        },
        "registries": [
            {
                "kind": "filesystem",
                "path": "a/b/c",
                "baseline": "default",
                "packages": [
                    "a",
                    "b",
                    "c"
                ]
            },
            {
                "kind": "git",
                "repository": "https://github.com/microsoft/vcpkg-ports",
                "baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
                "packages": [ 
                    "zlib",
                    "rapidjson",
                    "fmt"
                ]
            },
            {
                "kind": "artifact",
                "name": "vcpkg-artifacts",
                "location": "https://github.com/microsoft/vcpkg-artifacts"
            }
        ]
    })json";

    std::string raw = Strings::concat(R"json({
    "configuration": )json",
                                      raw_config,
                                      R"json(,
    "name": "zlib",
    "version": "1.0.0",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "dependencies": [
        "a",
        {
            "$extra": null,
            "name": "b"
        },
        {
            "name": "c",
            "version>=": "2018-09-01"
        }
    ]
})json");
    auto m_pgh = test_parse_port_manifest(raw);

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();
    REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
    REQUIRE(pgh.check_against_feature_flags({}, feature_flags_with_versioning));

    auto maybe_as_json = Json::parse(raw, "test");
    REQUIRE(maybe_as_json.has_value());
    auto as_json = *maybe_as_json.get();
    check_json_eq(Json::Value::object(serialize_manifest(pgh)), as_json.value);

    REQUIRE(pgh.core_paragraph->builtin_baseline == "089fa4de7dca22c67dcab631f618d5cd0697c8d4");
    REQUIRE(pgh.core_paragraph->dependencies.size() == 3);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "a");
    REQUIRE(pgh.core_paragraph->dependencies[0].constraint == DependencyConstraint{});
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "b");
    REQUIRE(pgh.core_paragraph->dependencies[1].constraint == DependencyConstraint{});
    REQUIRE(pgh.core_paragraph->dependencies[2].name == "c");
    REQUIRE(pgh.core_paragraph->dependencies[2].constraint ==
            DependencyConstraint{VersionConstraintKind::Minimum, Version{"2018-09-01", 0}});

    auto config = Json::parse(raw_config, "<test config>").value(VCPKG_LINE_INFO).value;
    REQUIRE(config.is_object());
    auto config_obj = config.object(VCPKG_LINE_INFO);
    REQUIRE(pgh.core_paragraph->configuration.has_value());
    auto parsed_config_obj = *pgh.core_paragraph->configuration.get();
    REQUIRE(Json::stringify(parsed_config_obj) == Json::stringify(config_obj));
    REQUIRE(pgh.core_paragraph->configuration_source == ConfigurationSource::ManifestFileConfiguration);
}

TEST_CASE ("manifest embed vcpkg_configuration", "[manifests]")
{
    std::string raw_config = R"json({
        "$extra-info": null,
        "default-registry": {
            "kind": "builtin",
            "baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4"
        },
        "registries": [
            {
                "kind": "filesystem",
                "path": "a/b/c",
                "baseline": "default",
                "packages": [
                    "a",
                    "b",
                    "c"
                ]
            },
            {
                "kind": "git",
                "repository": "https://github.com/microsoft/vcpkg-ports",
                "baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
                "packages": [ 
                    "zlib",
                    "rapidjson",
                    "fmt"
                ]
            },
            {
                "kind": "artifact",
                "name": "vcpkg-artifacts",
                "location": "https://github.com/microsoft/vcpkg-artifacts"
            }
        ]
    })json";

    std::string raw = Strings::concat(R"json({
    "vcpkg-configuration": )json",
                                      raw_config,
                                      R"json(,
    "name": "zlib",
    "version": "1.0.0",
    "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
    "dependencies": [
        "a",
        {
            "$extra": null,
            "name": "b"
        },
        {
            "name": "c",
            "version>=": "2018-09-01"
        }
    ]
})json");
    auto m_pgh = test_parse_port_manifest(raw);

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();
    REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
    REQUIRE(pgh.check_against_feature_flags({}, feature_flags_with_versioning));

    auto maybe_as_json = Json::parse(raw, "test");
    REQUIRE(maybe_as_json.has_value());
    auto as_json = *maybe_as_json.get();
    check_json_eq(Json::Value::object(serialize_manifest(pgh)), as_json.value);

    REQUIRE(pgh.core_paragraph->builtin_baseline == "089fa4de7dca22c67dcab631f618d5cd0697c8d4");
    REQUIRE(pgh.core_paragraph->dependencies.size() == 3);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "a");
    REQUIRE(pgh.core_paragraph->dependencies[0].constraint == DependencyConstraint{});
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "b");
    REQUIRE(pgh.core_paragraph->dependencies[1].constraint == DependencyConstraint{});
    REQUIRE(pgh.core_paragraph->dependencies[2].name == "c");
    REQUIRE(pgh.core_paragraph->dependencies[2].constraint ==
            DependencyConstraint{VersionConstraintKind::Minimum, Version{"2018-09-01", 0}});

    auto config = Json::parse(raw_config, "<test config>").value(VCPKG_LINE_INFO).value;
    REQUIRE(config.is_object());
    auto config_obj = config.object(VCPKG_LINE_INFO);
    REQUIRE(pgh.core_paragraph->configuration.has_value());
    auto parsed_config_obj = *pgh.core_paragraph->configuration.get();
    REQUIRE(Json::stringify(parsed_config_obj) == Json::stringify(config_obj));
    REQUIRE(pgh.core_paragraph->configuration_source == ConfigurationSource::ManifestFileVcpkgConfiguration);
}

TEST_CASE ("manifest construct maximum", "[manifests]")
{
    auto raw = R"json({
        "name": "s",
        "version-string": "v",
        "maintainers": "m",
        "contacts": { "a": { "aa": "aa" } },
        "summary": "d",
        "description": "d",
        "builtin-baseline": "123",
        "dependencies": ["bd"],
        "default-features": [
            "df",
            {
                "name": "zuko",
                "platform": "windows & arm"
            }
        ],
        "features": {
            "$feature-level-comment": "hi",
            "$feature-level-comment2": "123456",
            "iroh" : {
                "$comment": "hello",
                "description": "zuko's uncle",
                "dependencies": [
                    "firebending",
                    {
                        "name": "order-white-lotus",
                        "features": [
                            "the-ancient-ways",
                            {
                                "name": "windows-tests",
                                "platform": "windows"
                            }
                        ],
                        "platform": "!(windows & arm)"
                    },
                    {
                        "$extra": [],
                        "$my": [],
                        "name": "tea"
                    },
                    {
                        "name": "z-no-defaults",
                        "default-features": false
                    }
                ]
            },
            "zuko": {
                "description": ["son of the fire lord", "firebending 師父"],
                "supports": "!(windows & arm)",
                "license": "MIT"
            }
        }
})json";
    auto object = parse_json_object(raw);
    auto res = SourceControlFile::parse_port_manifest_object("<test manifest>", object, null_sink);
    if (!res.has_value())
    {
        msg::println(Color::error, res.error());
    }
    REQUIRE(res.has_value());
    REQUIRE(*res.get() != nullptr);
    auto& pgh = **res.get();

    REQUIRE(pgh.to_name() == "s");
    REQUIRE(pgh.to_version_scheme() == VersionScheme::String);
    REQUIRE(pgh.to_version() == Version{"v", 0});
    REQUIRE(pgh.core_paragraph->maintainers.size() == 1);
    REQUIRE(pgh.core_paragraph->maintainers[0] == "m");
    REQUIRE(pgh.core_paragraph->contacts.size() == 1);
    auto contact_a = pgh.core_paragraph->contacts.get("a");
    REQUIRE(contact_a);
    REQUIRE(contact_a->is_object());
    auto contact_a_aa = contact_a->object(VCPKG_LINE_INFO).get("aa");
    REQUIRE(contact_a_aa);
    REQUIRE(contact_a_aa->is_string());
    REQUIRE(contact_a_aa->string(VCPKG_LINE_INFO) == "aa");
    REQUIRE(pgh.core_paragraph->summary.size() == 1);
    REQUIRE(pgh.core_paragraph->summary[0] == "d");
    REQUIRE(pgh.core_paragraph->description.size() == 1);
    REQUIRE(pgh.core_paragraph->description[0] == "d");
    REQUIRE(pgh.core_paragraph->dependencies.size() == 1);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "bd");
    REQUIRE(pgh.core_paragraph->default_features.size() == 2);
    REQUIRE(pgh.core_paragraph->default_features[0].name == "df");
    REQUIRE(pgh.core_paragraph->default_features[0].platform.is_empty());
    REQUIRE(pgh.core_paragraph->default_features[1].name == "zuko");
    REQUIRE(to_string(pgh.core_paragraph->default_features[1].platform) == "windows & arm");
    REQUIRE(pgh.core_paragraph->supports_expression.is_empty());
    REQUIRE(pgh.core_paragraph->builtin_baseline == "123");

    REQUIRE(pgh.feature_paragraphs.size() == 2);

    REQUIRE(pgh.feature_paragraphs[0]->name == "iroh");
    REQUIRE(pgh.feature_paragraphs[0]->description.size() == 1);
    REQUIRE(pgh.feature_paragraphs[0]->description[0] == "zuko's uncle");
    REQUIRE(pgh.feature_paragraphs[0]->dependencies.size() == 4);
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[0].name == "firebending");

    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].name == "order-white-lotus");
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].default_features == true);
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].features.size() == 2);
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].features[0].name == "the-ancient-ways");
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].features[0].platform.is_empty());
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].features[1].name == "windows-tests");
    REQUIRE_FALSE(pgh.feature_paragraphs[0]->dependencies[1].features[1].platform.is_empty());
    REQUIRE(
        pgh.feature_paragraphs[0]->dependencies[1].features[1].platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    REQUIRE_FALSE(pgh.feature_paragraphs[0]->dependencies[1].features[1].platform.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));

    REQUIRE_FALSE(pgh.feature_paragraphs[0]->dependencies[1].platform.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].platform.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].platform.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));

    REQUIRE(pgh.feature_paragraphs[0]->dependencies[2].name == "tea");
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[3].name == "z-no-defaults");
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[3].default_features == false);

    REQUIRE(pgh.feature_paragraphs[1]->name == "zuko");
    REQUIRE(pgh.feature_paragraphs[1]->description.size() == 2);
    REQUIRE(pgh.feature_paragraphs[1]->description[0] == "son of the fire lord");
    REQUIRE(pgh.feature_paragraphs[1]->description[1] == "firebending 師父");
    REQUIRE(!pgh.feature_paragraphs[1]->supports_expression.is_empty());
    REQUIRE_FALSE(pgh.feature_paragraphs[1]->supports_expression.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    REQUIRE(pgh.feature_paragraphs[1]->supports_expression.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
    REQUIRE(pgh.feature_paragraphs[1]->license == parse_spdx_license_expression_required("MIT"));

    check_json_eq_ordered(serialize_manifest(pgh), object);
}

TEST_CASE ("SourceParagraph manifest two dependencies", "[manifests]")
{
    auto m_pgh = test_parse_port_manifest(R"json({
        "name": "zlib",
        "version-string": "1.2.8",
        "dependencies": ["z", "openssl"]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->dependencies.size() == 2);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "openssl");
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "z");
}

TEST_CASE ("SourceParagraph manifest three dependencies", "[manifests]")
{
    auto m_pgh = test_parse_port_manifest(R"json({
        "name": "zlib",
        "version-string": "1.2.8",
        "dependencies": ["z", "openssl", "xyz"]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->dependencies.size() == 3);
    // should be ordered
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "openssl");
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "xyz");
    REQUIRE(pgh.core_paragraph->dependencies[2].name == "z");
}

TEST_CASE ("SourceParagraph manifest construct qualified dependencies", "[manifests]")
{
    auto m_pgh = test_parse_port_manifest(R"json({
        "name": "zlib",
        "version-string": "1.2.8",
        "dependencies": [
            {
                "name": "liba",
                "platform": "windows"
            },
            {
                "name": "libb",
                "platform": "uwp"
            }
        ]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.to_name() == "zlib");
    REQUIRE(pgh.to_version_scheme() == VersionScheme::String);
    REQUIRE(pgh.to_version() == Version{"1.2.8", 0});
    REQUIRE(pgh.core_paragraph->maintainers.empty());
    REQUIRE(pgh.core_paragraph->description.empty());
    REQUIRE(pgh.core_paragraph->dependencies.size() == 2);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "liba");
    REQUIRE(pgh.core_paragraph->dependencies[0].platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "libb");
    REQUIRE(pgh.core_paragraph->dependencies[1].platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
}

TEST_CASE ("SourceParagraph manifest construct host dependencies", "[manifests]")
{
    std::string raw = R"json({
    "name": "zlib",
    "version-string": "1.2.8",
    "dependencies": [
        {
            "name": "liba",
            "host": true
        },
        "libb"
    ]
}
)json";
    auto m_pgh = test_parse_port_manifest(raw);
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.to_name() == "zlib");
    REQUIRE(pgh.to_version_scheme() == VersionScheme::String);
    REQUIRE(pgh.to_version() == Version{"1.2.8", 0});
    REQUIRE(pgh.core_paragraph->maintainers.empty());
    REQUIRE(pgh.core_paragraph->description.empty());
    REQUIRE(pgh.core_paragraph->dependencies.size() == 2);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "liba");
    REQUIRE(pgh.core_paragraph->dependencies[0].host);
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "libb");
    REQUIRE(!pgh.core_paragraph->dependencies[1].host);

    REQUIRE(Json::stringify(serialize_manifest(pgh), Json::JsonStyle::with_spaces(4)) == raw);
}

TEST_CASE ("SourceParagraph manifest default features", "[manifests]")
{
    auto m_pgh = test_parse_port_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "default-features": ["a1"]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->default_features.size() == 1);
    REQUIRE(pgh.core_paragraph->default_features[0].name == "a1");
    REQUIRE(pgh.core_paragraph->default_features[0].platform.is_empty());
}

TEST_CASE ("SourceParagraph manifest default feature missing name", "[manifests]")
{
    auto m_pgh = test_parse_port_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "default-features": [{"platform": "!windows"}]
    })json",
                                          PrintErrors::No);
    REQUIRE(!m_pgh.has_value());

    m_pgh = test_parse_port_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "default-features": [{"name": "", "platform": "!windows"}]
    })json",
                                     PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
}

TEST_CASE ("SourceParagraph manifest description paragraph", "[manifests]")
{
    auto m_pgh = test_parse_port_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "description": ["line 1", "line 2", "line 3"]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->description.size() == 3);
    REQUIRE(pgh.core_paragraph->description[0] == "line 1");
    REQUIRE(pgh.core_paragraph->description[1] == "line 2");
    REQUIRE(pgh.core_paragraph->description[2] == "line 3");
}

TEST_CASE ("SourceParagraph manifest supports", "[manifests]")
{
    auto m_pgh = test_parse_port_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "supports": "!(windows | osx)"
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->supports_expression.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    REQUIRE_FALSE(pgh.core_paragraph->supports_expression.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    REQUIRE_FALSE(pgh.core_paragraph->supports_expression.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));
}

TEST_CASE ("SourceParagraph manifest empty supports", "[manifests]")
{
    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "a",
        "version-string": "1.0",
        "supports": ""
    })json"));
}

TEST_CASE ("SourceParagraph manifest non-string supports", "[manifests]")
{
    REQUIRE_FALSE(port_manifest_is_parsable(R"json({
        "name": "a",
        "version-string": "1.0",
        "supports": true
    })json"));
}

static Json::Object manifest_with_license(Json::Value&& license)
{
    Json::Object res;
    res.insert("name", Json::Value::string("foo"));
    res.insert("version", Json::Value::string("0"));
    res.insert("license", std::move(license));
    return res;
}
static Json::Object manifest_with_license(StringView license)
{
    return manifest_with_license(Json::Value::string(license.to_string()));
}
static std::string test_serialized_license(StringView license)
{
    auto m_pgh = test_parse_port_manifest(manifest_with_license(license));
    REQUIRE(m_pgh.has_value());

    return serialize_manifest(**m_pgh.get())["license"].string(VCPKG_LINE_INFO).to_string();
}

static bool license_is_parsable(StringView license)
{
    ParseMessages messages;
    parse_spdx_license_expression(license, messages);
    return !messages.any_errors();
}
static bool license_is_strict(StringView license)
{
    ParseMessages messages;
    parse_spdx_license_expression(license, messages);
    return messages.good();
}

TEST_CASE ("simple license in manifest", "[manifests][license]")
{
    CHECK(port_manifest_is_parsable(manifest_with_license(Json::Value::null(nullptr))));
    CHECK_FALSE(port_manifest_is_parsable(manifest_with_license("")));
    CHECK(port_manifest_is_parsable(manifest_with_license("MIT")));
}

TEST_CASE ("valid and invalid licenses", "[manifests][license]")
{
    CHECK(license_is_strict("mIt"));
    CHECK(license_is_strict("Apache-2.0"));
    CHECK(license_is_strict("GPL-2.0+"));
    CHECK_FALSE(license_is_parsable("GPL-2.0++"));
    CHECK(license_is_strict("LicenseRef-blah"));
    CHECK_FALSE(license_is_strict("unknownlicense"));
    CHECK(license_is_parsable("unknownlicense"));
}

TEST_CASE ("licenses with compounds", "[manifests][license]")
{
    CHECK(license_is_strict("GPL-3.0+ WITH GCC-exception-3.1"));
    CHECK(license_is_strict("Apache-2.0 WITH LLVM-exception"));
    CHECK_FALSE(license_is_parsable("(Apache-2.0) WITH LLVM-exception"));
    CHECK(license_is_strict("(Apache-2.0 OR MIT) AND GPL-3.0+ WITH GCC-exception-3.1"));
    CHECK_FALSE(license_is_parsable("Apache-2.0 WITH"));
    CHECK_FALSE(license_is_parsable("GPL-3.0+ AND"));
    CHECK_FALSE(license_is_parsable("MIT and Apache-2.0"));
    CHECK_FALSE(license_is_parsable("GPL-3.0 WITH GCC-exception+"));
    CHECK_FALSE(license_is_parsable("(GPL-3.0 WITH GCC-exception)+"));
}

TEST_CASE ("license serialization", "[manifests][license]")
{
    auto m_pgh = test_parse_port_manifest(manifest_with_license(Json::Value::null(nullptr)));
    REQUIRE(m_pgh);
    auto manifest = serialize_manifest(**m_pgh.get());
    REQUIRE(manifest.contains("license"));
    CHECK(manifest["license"].is_null());

    CHECK(test_serialized_license("MIT") == "MIT");
    CHECK(test_serialized_license("mit") == "MIT");
    CHECK(test_serialized_license("MiT    AND (aPACHe-2.0 \tOR   \n gpl-2.0+)") == "MIT AND (Apache-2.0 OR GPL-2.0+)");
    CHECK(test_serialized_license("MiT    AND (BsL-1.0) AND (aPACHe-2.0 \tOR   \n gpl-2.0+)") ==
          "MIT AND BSL-1.0 AND (Apache-2.0 OR GPL-2.0+)");
    CHECK(test_serialized_license("MiT    AND (aPACHe-2.0 AND (BSL-1.0) \tOR   \n gpl-2.0+)") ==
          "MIT AND (Apache-2.0 AND BSL-1.0 OR GPL-2.0+)");
    CHECK(test_serialized_license("uNkNoWnLiCeNsE") == "uNkNoWnLiCeNsE");
}

TEST_CASE ("license-list-extraction", "[manifests]")
{
    ParseMessages messages;
    const auto mit = parse_spdx_license_expression("MIT", messages);
    REQUIRE(messages.good());
    REQUIRE(mit.kind() == SpdxLicenseDeclarationKind::String);
    REQUIRE(mit.license_text() == "MIT");
    REQUIRE(mit.applicable_licenses() ==
            std::vector<SpdxApplicableLicenseExpression>{SpdxApplicableLicenseExpression{"MIT", false}});

    const auto mit_and_boost = parse_spdx_license_expression("MIT AND BSL-1.0", messages);
    REQUIRE(messages.good());
    REQUIRE(mit_and_boost.kind() == SpdxLicenseDeclarationKind::String);
    REQUIRE(mit_and_boost.license_text() == "MIT AND BSL-1.0");
    REQUIRE(mit_and_boost.applicable_licenses() ==
            std::vector<SpdxApplicableLicenseExpression>{SpdxApplicableLicenseExpression{"BSL-1.0", false},
                                                         SpdxApplicableLicenseExpression{"MIT", false}});

    const auto parens = parse_spdx_license_expression("(MIT) AND (BSL-1.0 AND Apache-2.0)", messages);
    REQUIRE(messages.good());
    REQUIRE(parens.kind() == SpdxLicenseDeclarationKind::String);
    REQUIRE(parens.license_text() == "MIT AND BSL-1.0 AND Apache-2.0");
    REQUIRE(parens.applicable_licenses() ==
            std::vector<SpdxApplicableLicenseExpression>{SpdxApplicableLicenseExpression{"Apache-2.0", false},
                                                         SpdxApplicableLicenseExpression{"BSL-1.0", false},
                                                         SpdxApplicableLicenseExpression{"MIT", false}});

    const auto complex =
        parse_spdx_license_expression("MiT    AND (aPACHe-2.0 AND (BSL-1.0) \tOR   \n gpl-2.0+)", messages);
    REQUIRE(messages.good());
    REQUIRE(complex.kind() == SpdxLicenseDeclarationKind::String);
    REQUIRE(complex.license_text() == "MIT AND (Apache-2.0 AND BSL-1.0 OR GPL-2.0+)");
    REQUIRE(complex.applicable_licenses() ==
            std::vector<SpdxApplicableLicenseExpression>{
                SpdxApplicableLicenseExpression{"Apache-2.0 AND BSL-1.0 OR GPL-2.0+", true},
                SpdxApplicableLicenseExpression{"MIT", false}});

    const auto complex2 = parse_spdx_license_expression(
        "MIT AND BSL-1.0 AND (Apache-2.0 AND BSL-1.0 OR (GPL-2.0+ OR MIT AND BSD-3-Clause))", messages);
    REQUIRE(messages.good());
    REQUIRE(complex2.license_text() ==
            "MIT AND BSL-1.0 AND (Apache-2.0 AND BSL-1.0 OR (GPL-2.0+ OR MIT AND BSD-3-Clause))");
    REQUIRE(complex2.applicable_licenses() ==
            std::vector<SpdxApplicableLicenseExpression>{
                SpdxApplicableLicenseExpression{"Apache-2.0 AND BSL-1.0 OR (GPL-2.0+ OR MIT AND BSD-3-Clause)", true},
                SpdxApplicableLicenseExpression{"BSL-1.0", false},
                SpdxApplicableLicenseExpression{"MIT", false}});

    // note that these ORs could be collapsed together, but we don't attempt that
    const auto many_or = parse_spdx_license_expression("MIT OR BSL-1.0 OR Apache-2.0", messages);
    REQUIRE(messages.good());
    REQUIRE(many_or.license_text() == "MIT OR BSL-1.0 OR Apache-2.0");
    // note that 'requires parens' is true because when combining these ORs for the overall AND that
    // goes into an SBOM, putting parens around it are required
    REQUIRE(many_or.applicable_licenses() ==
            std::vector<SpdxApplicableLicenseExpression>{
                SpdxApplicableLicenseExpression{"MIT OR BSL-1.0 OR Apache-2.0", true}});
}

TEST_CASE ("license error messages", "[manifests][license]")
{
    ParseMessages messages;
    parse_spdx_license_expression("", messages);
    CHECK(messages.join() == LocalizedString::from_raw(R"(<license string>: error: SPDX license expression was empty.
  on expression: 
                 ^)"));

    parse_spdx_license_expression("MIT ()", messages);
    CHECK(messages.join() ==
          LocalizedString::from_raw(
              R"(<license string>: error: Expected a compound or the end of the string, found a parenthesis.
  on expression: MIT ()
                     ^)"));

    parse_spdx_license_expression("MIT +", messages);
    CHECK(
        messages.join() ==
        LocalizedString::from_raw(
            R"(<license string>: error: SPDX license expression contains an extra '+'. These are only allowed directly after a license identifier.
  on expression: MIT +
                     ^)"));

    parse_spdx_license_expression("MIT AND", messages);
    CHECK(messages.join() ==
          LocalizedString::from_raw(R"(<license string>: error: Expected a license name, found the end of the string.
  on expression: MIT AND
                        ^)"));

    parse_spdx_license_expression("MIT MIT", messages);
    CHECK(messages.join() ==
          LocalizedString::from_raw(
              R"(<license string>: error: Expected either AND, OR, or WITH, found a license or exception name: 'MIT'.
  on expression: MIT MIT
                     ^)"));

    parse_spdx_license_expression("MIT AND unknownlicense", messages);
    CHECK(!messages.any_errors());
    CHECK(
        messages.join() ==
        R"(<license string>: warning: Unknown license identifier 'unknownlicense'. Known values are listed at https://spdx.org/licenses/
  on expression: MIT AND unknownlicense
                         ^)");
}

TEST_CASE ("default-feature-core errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "default-features": ["core"]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.default-features[0] (a default feature): the feature \"core\" turns off default "
            "features and thus can't be in the default features list");
}

TEST_CASE ("default-feature-core-object errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "default-features": [ { "name": "core" } ]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.default-features[0].name (a default feature): the feature \"core\" turns off "
            "default features and thus can't be in the default features list");
}

TEST_CASE ("default-feature-default errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "default-features": ["default"]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.default-features[0] (a default feature): the feature \"default\" refers to the "
            "set of default features and thus can't be in the default features list");
}

TEST_CASE ("default-feature-default-object errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "default-features": [ { "name": "default" } ]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.default-features[0].name (a default feature): the feature \"default\" refers to "
            "the set of default features and thus can't be in the default features list");
}

TEST_CASE ("default-feature-empty errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "default-features": [""]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.default-features[0] (a feature name): \"\" is not a valid feature name. Feature "
            "names must be lowercase alphanumeric+hyphens and not reserved (see " +
                docs::manifests_url + " for more information).");
}

TEST_CASE ("default-feature-empty-object errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "default-features": [ { "name": "" } ]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.default-features[0].name (a feature name): \"\" is not a valid feature name. "
            "Feature names must be lowercase alphanumeric+hyphens and not reserved (see " +
                docs::manifests_url + " for more information).");
}

TEST_CASE ("dependency-name-empty errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "dependencies": [ "" ]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.dependencies[0] (a package name): \"\" is not a valid package name. Package "
            "names must be lowercase alphanumeric+hyphens and not reserved (see " +
                docs::manifests_url + " for more information).");
}

TEST_CASE ("dependency-name-empty-object errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "dependencies": [ { "name": "" } ]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.dependencies[0].name (a package name): \"\" is not a valid package name. "
            "Package names must be lowercase alphanumeric+hyphens and not reserved (see " +
                docs::manifests_url + " for more information).");
}

TEST_CASE ("dependency-feature-name-core errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "dependencies": [
        {
          "name": "icu",
          "features": [ "core" ]
        }
      ]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.dependencies[0].features[0] (a feature name): the feature \"core\" cannot be in "
            "a dependency's feature list. To turn off default features, add \"default-features\": false instead.");
}

TEST_CASE ("dependency-feature-name-core-object errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "dependencies": [
        {
          "name": "icu",
          "features": [ { "name": "core" } ]
        }
      ]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(
        m_pgh.error().data() ==
        "<test manifest>: error: $.dependencies[0].features[0].name (a feature name): the feature \"core\" cannot be "
        "in a dependency's feature list. To turn off default features, add \"default-features\": false instead.");
}

TEST_CASE ("dependency-feature-name-default errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "dependencies": [
        {
          "name": "icu",
          "features": [ "default" ]
        }
      ]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.dependencies[0].features[0] (a feature name): the feature \"default\" cannot be "
            "in a dependency's feature list. To turn on default features, add \"default-features\": true instead.");
}

TEST_CASE ("dependency-feature-name-default-object errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "dependencies": [
        {
          "name": "icu",
          "features": [ { "name": "default" } ]
        }
      ]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(
        m_pgh.error().data() ==
        "<test manifest>: error: $.dependencies[0].features[0].name (a feature name): the feature \"default\" cannot "
        "be in a dependency's feature list. To turn on default features, add \"default-features\": true instead.");
}
TEST_CASE ("dependency-feature-name-empty errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "dependencies": [
        {
          "name": "icu",
          "features": [ "" ]
        }
      ]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.dependencies[0].features[0] (a feature name): \"\" is not a valid feature name. "
            "Feature names must be lowercase alphanumeric+hyphens and not reserved (see " +
                docs::manifests_url + " for more information).");
}

TEST_CASE ("dependency-feature-name-empty-object errors", "[manifests]")
{
    auto m_pgh = test_parse_project_manifest(R"json({
      "dependencies": [
        {
          "name": "icu",
          "features": [ { "name": "" } ]
        }
      ]
    })json",
                                             PrintErrors::No);
    REQUIRE(!m_pgh.has_value());
    REQUIRE(m_pgh.error().data() ==
            "<test manifest>: error: $.dependencies[0].features[0].name (a feature name): \"\" is not a valid feature "
            "name. Feature names must be lowercase alphanumeric+hyphens and not reserved (see " +
                docs::manifests_url + " for more information).");
}
