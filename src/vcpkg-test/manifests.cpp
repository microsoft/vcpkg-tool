#include <catch2/catch.hpp>

#include <vcpkg/base/json.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/paragraphs.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <vcpkg-test/util.h>

using namespace vcpkg;
using namespace vcpkg::Paragraphs;
using namespace vcpkg::Test;

static Json::Object parse_json_object(StringView sv)
{
    auto json = Json::parse(sv);
    // we're not testing json parsing here, so just fail on errors
    if (auto r = json.get())
    {
        return std::move(r->first.object());
    }
    else
    {
        vcpkg::print2("Error found while parsing JSON document:\n", sv, '\n');
        Checks::exit_with_message(VCPKG_LINE_INFO, json.error()->format());
    }
}

enum class PrintErrors : bool
{
    No,
    Yes,
};

static ParseExpected<SourceControlFile> test_parse_manifest(const Json::Object& obj,
                                                            PrintErrors print = PrintErrors::Yes)
{
    auto res = SourceControlFile::parse_manifest_object("<test manifest>", obj);
    if (!res.has_value() && print == PrintErrors::Yes)
    {
        print_error_message(res.error());
    }
    return res;
}
static ParseExpected<SourceControlFile> test_parse_manifest(StringView obj, PrintErrors print = PrintErrors::Yes)
{
    return test_parse_manifest(parse_json_object(obj), print);
}

static bool manifest_is_parseable(const Json::Object& obj)
{
    return test_parse_manifest(obj, PrintErrors::No).has_value();
}
static bool manifest_is_parseable(StringView obj)
{
    return test_parse_manifest(parse_json_object(obj), PrintErrors::No).has_value();
}

static const FeatureFlagSettings feature_flags_with_versioning{false, false, false, true};
static const FeatureFlagSettings feature_flags_without_versioning{false, false, false, false};

TEST_CASE ("manifest construct minimum", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "zlib",
        "version-string": "1.2.8"
    })json");

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->name == "zlib");
    REQUIRE(pgh.core_paragraph->raw_version == "1.2.8");
    REQUIRE(pgh.core_paragraph->maintainers.empty());
    REQUIRE(pgh.core_paragraph->contacts.is_empty());
    REQUIRE(pgh.core_paragraph->summary.empty());
    REQUIRE(pgh.core_paragraph->description.empty());
    REQUIRE(pgh.core_paragraph->dependencies.empty());
    REQUIRE(!pgh.core_paragraph->builtin_baseline.has_value());
    REQUIRE(!pgh.core_paragraph->vcpkg_configuration.has_value());

    REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
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
        auto m_pgh = test_parse_manifest(std::get<0>(v));
        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        CHECK(Json::stringify(serialize_manifest(pgh), Json::JsonStyle::with_spaces(4)) == std::get<0>(v));
        CHECK(pgh.core_paragraph->version_scheme == std::get<1>(v));
        CHECK(pgh.core_paragraph->raw_version == std::get<2>(v));
        CHECK(pgh.core_paragraph->port_version == 0);
    }

    REQUIRE_FALSE(manifest_is_parseable(R"json({
        "name": "zlib",
        "version-string": "abcd",
        "version-semver": "1.2.3-rc3"
    })json"));

    REQUIRE_FALSE(manifest_is_parseable(R"json({
        "name": "zlib",
        "version-string": "abcd#1"
    })json"));
    REQUIRE_FALSE(manifest_is_parseable(R"json({
        "name": "zlib",
        "version": "abcd#1"
    })json"));
    REQUIRE_FALSE(manifest_is_parseable(R"json({
        "name": "zlib",
        "version-date": "abcd#1"
    })json"));
    REQUIRE_FALSE(manifest_is_parseable(R"json({
        "name": "zlib",
        "version-semver": "abcd#1"
    })json"));

    SECTION ("version syntax")
    {
        REQUIRE_FALSE(manifest_is_parseable(R"json({
        "name": "zlib",
        "version-semver": "2020-01-01"
    })json"));
        REQUIRE_FALSE(manifest_is_parseable(R"json({
        "name": "zlib",
        "version-date": "1.1.1"
    })json"));
        REQUIRE(manifest_is_parseable(R"json({
        "name": "zlib",
        "version": "1.2.3-rc3"
    })json"));
    }
}

TEST_CASE ("manifest constraints hash", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
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
    REQUIRE(p->core_paragraph->dependencies.at(0).constraint.value == "2018-09-01");
    REQUIRE(p->core_paragraph->dependencies.at(0).constraint.port_version == 1);

    REQUIRE_FALSE(manifest_is_parseable(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "dependencies": [
        {
            "name": "d",
            "version>=": "2018-09-01#0"
        }
    ]
})json"));

    REQUIRE_FALSE(manifest_is_parseable(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "dependencies": [
        {
            "name": "d",
            "version>=": "2018-09-01#-1"
        }
    ]
})json"));

    REQUIRE_FALSE(manifest_is_parseable(R"json({
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
    REQUIRE_FALSE(manifest_is_parseable(R"json({
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
    REQUIRE_FALSE(manifest_is_parseable(R"json({
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
    REQUIRE_FALSE(manifest_is_parseable(R"json({
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
    REQUIRE_FALSE(manifest_is_parseable(R"json({
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

    auto parsed = test_parse_manifest(R"json({
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
    CHECK((*parsed.get())->core_paragraph->overrides.at(0).port_version == 1);

    parsed = test_parse_manifest(R"json({
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
    CHECK((*parsed.get())->core_paragraph->overrides.at(0).port_version == 1);

    parsed = test_parse_manifest(R"json({
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
    CHECK((*parsed.get())->core_paragraph->overrides.at(0).port_version == 1);

    parsed = test_parse_manifest(R"json({
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
    CHECK((*parsed.get())->core_paragraph->overrides.at(0).port_version == 1);
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
    auto m_pgh = test_parse_manifest(raw);

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();
    REQUIRE(pgh.check_against_feature_flags({}, feature_flags_without_versioning));
    REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_with_versioning));
    REQUIRE(Json::stringify(serialize_manifest(pgh), Json::JsonStyle::with_spaces(4)) == raw);
    REQUIRE(pgh.core_paragraph->dependencies.size() == 3);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "a");
    REQUIRE(pgh.core_paragraph->dependencies[0].constraint == DependencyConstraint{VersionConstraintKind::None, "", 0});
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "c");
    REQUIRE(pgh.core_paragraph->dependencies[1].constraint == DependencyConstraint{VersionConstraintKind::None, "", 0});
    REQUIRE(pgh.core_paragraph->dependencies[2].name == "d");
    REQUIRE(pgh.core_paragraph->dependencies[2].constraint ==
            DependencyConstraint{VersionConstraintKind::Minimum, "2018-09-01", 0});
    REQUIRE(pgh.core_paragraph->builtin_baseline == "089fa4de7dca22c67dcab631f618d5cd0697c8d4");

    REQUIRE_FALSE(manifest_is_parseable(R"json({
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
        auto m_pgh = test_parse_manifest(raw);

        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_with_versioning));
        REQUIRE(pgh.core_paragraph->builtin_baseline.value_or("does not have a value") ==
                "089fa4de7dca22c67dcab631f618d5cd0697c8d4");
    }

    SECTION ("empty baseline")
    {
        std::string raw = R"json({
    "name": "zlib",
    "version-string": "abcd",
    "builtin-baseline": ""
}
)json";

        auto m_pgh = test_parse_manifest(raw);

        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_with_versioning));
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

        auto m_pgh = test_parse_manifest(raw);
        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(pgh.core_paragraph->dependencies.size() == 1);
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.value == "abcd");
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.port_version == 1);
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.type == VersionConstraintKind::Minimum);
        REQUIRE(pgh.core_paragraph->overrides.size() == 1);
        REQUIRE(pgh.core_paragraph->overrides[0].version_scheme == VersionScheme::String);
        REQUIRE(pgh.core_paragraph->overrides[0].version == "abcd");
        REQUIRE(pgh.core_paragraph->overrides[0].port_version == 0);
        REQUIRE(pgh.core_paragraph->builtin_baseline.value_or("does not have a value") ==
                "089fa4de7dca22c67dcab631f618d5cd0697c8d4");
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_with_versioning));
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

        auto m_pgh = test_parse_manifest(raw);
        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(pgh.core_paragraph->dependencies.size() == 1);
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.value == "abcd");
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.port_version == 1);
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.type == VersionConstraintKind::Minimum);
        REQUIRE(pgh.core_paragraph->overrides.size() == 1);
        REQUIRE(pgh.core_paragraph->overrides[0].version_scheme == VersionScheme::String);
        REQUIRE(pgh.core_paragraph->overrides[0].version == "abcd");
        REQUIRE(pgh.core_paragraph->overrides[0].port_version == 0);
        REQUIRE(!pgh.core_paragraph->builtin_baseline.has_value());
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_with_versioning));
    }
}

TEST_CASE ("manifest overrides", "[manifests]")
{
    std::tuple<StringLiteral, VersionScheme, StringLiteral> data[] = {
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
         VersionScheme::String,
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
         VersionScheme::Date,
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
         VersionScheme::Relaxed,
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
         VersionScheme::Semver,
         "1.2.3-rc3"},
    };
    for (auto&& v : data)
    {
        auto m_pgh = test_parse_manifest(std::get<0>(v));

        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(Json::stringify(serialize_manifest(pgh), Json::JsonStyle::with_spaces(4)) == std::get<0>(v));
        REQUIRE(pgh.core_paragraph->overrides.size() == 1);
        REQUIRE(pgh.core_paragraph->overrides[0].version_scheme == std::get<1>(v));
        REQUIRE(pgh.core_paragraph->overrides[0].version == std::get<2>(v));
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_with_versioning));
    }

    REQUIRE_FALSE(manifest_is_parseable(R"json({
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

    REQUIRE_FALSE(manifest_is_parseable(R"json({
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
    auto m_pgh = test_parse_manifest(raw);

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();
    REQUIRE(Json::stringify(serialize_manifest(pgh), Json::JsonStyle::with_spaces(4)) == raw);
    REQUIRE(pgh.core_paragraph->overrides.size() == 2);
    REQUIRE(pgh.core_paragraph->overrides[0].name == "abc");
    REQUIRE(pgh.core_paragraph->overrides[0].port_version == 5);
    REQUIRE(pgh.core_paragraph->overrides[1].name == "abcd");
    REQUIRE(pgh.core_paragraph->overrides[1].port_version == 7);

    REQUIRE(pgh.check_against_feature_flags({}, feature_flags_without_versioning));
    REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_with_versioning));
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
    auto m_pgh = test_parse_manifest(raw);

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();
    REQUIRE(pgh.check_against_feature_flags({}, feature_flags_without_versioning));
    REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_with_versioning));

    auto maybe_as_json = Json::parse(raw);
    REQUIRE(maybe_as_json.has_value());
    auto as_json = *maybe_as_json.get();
    check_json_eq(Json::Value::object(serialize_manifest(pgh)), as_json.first);

    REQUIRE(pgh.core_paragraph->builtin_baseline == "089fa4de7dca22c67dcab631f618d5cd0697c8d4");
    REQUIRE(pgh.core_paragraph->dependencies.size() == 3);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "a");
    REQUIRE(pgh.core_paragraph->dependencies[0].constraint == DependencyConstraint{VersionConstraintKind::None, "", 0});
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "b");
    REQUIRE(pgh.core_paragraph->dependencies[1].constraint == DependencyConstraint{VersionConstraintKind::None, "", 0});
    REQUIRE(pgh.core_paragraph->dependencies[2].name == "c");
    REQUIRE(pgh.core_paragraph->dependencies[2].constraint ==
            DependencyConstraint{VersionConstraintKind::Minimum, "2018-09-01", 0});

    auto maybe_config = Json::parse(raw_config, "<test config>");
    REQUIRE(maybe_config.has_value());
    auto config = *maybe_config.get();
    REQUIRE(config.first.is_object());
    auto config_obj = config.first.object();
    REQUIRE(pgh.core_paragraph->vcpkg_configuration.has_value());
    auto parsed_config_obj = *pgh.core_paragraph->vcpkg_configuration.get();
    REQUIRE(Json::stringify(parsed_config_obj, Json::JsonStyle::with_spaces(4)) ==
            Json::stringify(config_obj, Json::JsonStyle::with_spaces(4)));
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
        "default-features": ["df"],
        "features": {
            "$feature-level-comment": "hi",
            "$feature-level-comment2": "123456",
            "iroh" : {
                "$comment": "hello",
                "description": "zuko's uncle",
                "dependencies": [
                    "firebending",
                    {
                        "name": "order.white-lotus",
                        "features": [ "the-ancient-ways" ],
                        "platform": "!(windows & arm)"
                },
                {
                    "$extra": [],
                    "$my": [],
                    "name": "tea"
                    }
                ]
            },
            "zuko": {
                "description": ["son of the fire lord", "firebending 師父"],
                "supports": "!(windows & arm)"
            }
        }
})json";
    auto object = parse_json_object(raw);
    auto res = SourceControlFile::parse_manifest_object("<test manifest>", object);
    if (!res.has_value())
    {
        print_error_message(res.error());
    }
    REQUIRE(res.has_value());
    REQUIRE(*res.get() != nullptr);
    auto& pgh = **res.get();

    REQUIRE(pgh.core_paragraph->name == "s");
    REQUIRE(pgh.core_paragraph->raw_version == "v");
    REQUIRE(pgh.core_paragraph->maintainers.size() == 1);
    REQUIRE(pgh.core_paragraph->maintainers[0] == "m");
    REQUIRE(pgh.core_paragraph->contacts.size() == 1);
    auto contact_a = pgh.core_paragraph->contacts.get("a");
    REQUIRE(contact_a);
    REQUIRE(contact_a->is_object());
    auto contact_a_aa = contact_a->object().get("aa");
    REQUIRE(contact_a_aa);
    REQUIRE(contact_a_aa->is_string());
    REQUIRE(contact_a_aa->string() == "aa");
    REQUIRE(pgh.core_paragraph->summary.size() == 1);
    REQUIRE(pgh.core_paragraph->summary[0] == "d");
    REQUIRE(pgh.core_paragraph->description.size() == 1);
    REQUIRE(pgh.core_paragraph->description[0] == "d");
    REQUIRE(pgh.core_paragraph->dependencies.size() == 1);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "bd");
    REQUIRE(pgh.core_paragraph->default_features.size() == 1);
    REQUIRE(pgh.core_paragraph->default_features[0] == "df");
    REQUIRE(pgh.core_paragraph->supports_expression.is_empty());
    REQUIRE(pgh.core_paragraph->builtin_baseline == "123");

    REQUIRE(pgh.feature_paragraphs.size() == 2);

    REQUIRE(pgh.feature_paragraphs[0]->name == "iroh");
    REQUIRE(pgh.feature_paragraphs[0]->description.size() == 1);
    REQUIRE(pgh.feature_paragraphs[0]->description[0] == "zuko's uncle");
    REQUIRE(pgh.feature_paragraphs[0]->dependencies.size() == 3);
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[0].name == "firebending");

    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].name == "order.white-lotus");
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].features.size() == 1);
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].features[0] == "the-ancient-ways");
    REQUIRE_FALSE(pgh.feature_paragraphs[0]->dependencies[1].platform.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].platform.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].platform.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));

    REQUIRE(pgh.feature_paragraphs[0]->dependencies[2].name == "tea");

    REQUIRE(pgh.feature_paragraphs[1]->name == "zuko");
    REQUIRE(pgh.feature_paragraphs[1]->description.size() == 2);
    REQUIRE(pgh.feature_paragraphs[1]->description[0] == "son of the fire lord");
    REQUIRE(pgh.feature_paragraphs[1]->description[1] == "firebending 師父");
    REQUIRE(!pgh.feature_paragraphs[1]->supports_expression.is_empty());
    REQUIRE_FALSE(pgh.feature_paragraphs[1]->supports_expression.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    REQUIRE(pgh.feature_paragraphs[1]->supports_expression.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));

    check_json_eq_ordered(serialize_manifest(pgh), object);
}

TEST_CASE ("SourceParagraph manifest two dependencies", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
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
    auto m_pgh = test_parse_manifest(R"json({
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
    auto m_pgh = test_parse_manifest(R"json({
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

    REQUIRE(pgh.core_paragraph->name == "zlib");
    REQUIRE(pgh.core_paragraph->raw_version == "1.2.8");
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
    auto m_pgh = test_parse_manifest(raw);
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->name == "zlib");
    REQUIRE(pgh.core_paragraph->raw_version == "1.2.8");
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
    auto m_pgh = test_parse_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "default-features": ["a1"]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->default_features.size() == 1);
    REQUIRE(pgh.core_paragraph->default_features[0] == "a1");
}

TEST_CASE ("SourceParagraph manifest description paragraph", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
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
    auto m_pgh = test_parse_manifest(R"json({
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
    REQUIRE_FALSE(manifest_is_parseable(R"json({
        "name": "a",
        "version-string": "1.0",
        "supports": ""
    })json"));
}

TEST_CASE ("SourceParagraph manifest non-string supports", "[manifests]")
{
    REQUIRE_FALSE(manifest_is_parseable(R"json({
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
    auto m_pgh = test_parse_manifest(manifest_with_license(license));
    REQUIRE(m_pgh.has_value());

    return serialize_manifest(**m_pgh.get())["license"].string().to_string();
}

static bool license_is_parseable(StringView license)
{
    ParseMessages messages;
    parse_spdx_license_expression(license, messages);
    return messages.error == nullptr;
}
static bool license_is_strict(StringView license)
{
    ParseMessages messages;
    parse_spdx_license_expression(license, messages);
    return messages.error == nullptr && messages.warnings.empty();
}

static std::string test_format_parse_warning(const ParseMessage& msg)
{
    return msg.format("<license string>", MessageKind::Warning).extract_data();
}

TEST_CASE ("simple license in manifest", "[manifests][license]")
{
    CHECK(manifest_is_parseable(manifest_with_license(Json::Value::null(nullptr))));
    CHECK_FALSE(manifest_is_parseable(manifest_with_license("")));
    CHECK(manifest_is_parseable(manifest_with_license("MIT")));
}

TEST_CASE ("valid and invalid licenses", "[manifests][license]")
{
    CHECK(license_is_strict("mIt"));
    CHECK(license_is_strict("Apache-2.0"));
    CHECK(license_is_strict("GPL-2.0+"));
    CHECK_FALSE(license_is_parseable("GPL-2.0++"));
    CHECK(license_is_strict("LicenseRef-blah"));
    CHECK_FALSE(license_is_strict("unknownlicense"));
    CHECK(license_is_parseable("unknownlicense"));
}

TEST_CASE ("licenses with compounds", "[manifests][license]")
{
    CHECK(license_is_strict("GPL-3.0+ WITH GCC-exception-3.1"));
    CHECK(license_is_strict("Apache-2.0 WITH LLVM-exception"));
    CHECK_FALSE(license_is_parseable("(Apache-2.0) WITH LLVM-exception"));
    CHECK(license_is_strict("(Apache-2.0 OR MIT) AND GPL-3.0+ WITH GCC-exception-3.1"));
    CHECK_FALSE(license_is_parseable("Apache-2.0 WITH"));
    CHECK_FALSE(license_is_parseable("GPL-3.0+ AND"));
    CHECK_FALSE(license_is_parseable("MIT and Apache-2.0"));
    CHECK_FALSE(license_is_parseable("GPL-3.0 WITH GCC-exception+"));
    CHECK_FALSE(license_is_parseable("(GPL-3.0 WITH GCC-exception)+"));
}

TEST_CASE ("license serialization", "[manifests][license]")
{
    auto m_pgh = test_parse_manifest(manifest_with_license(Json::Value::null(nullptr)));
    REQUIRE(m_pgh);
    auto manifest = serialize_manifest(**m_pgh.get());
    REQUIRE(manifest.contains("license"));
    CHECK(manifest["license"].is_null());

    CHECK(test_serialized_license("MIT") == "MIT");
    CHECK(test_serialized_license("mit") == "MIT");
    CHECK(test_serialized_license("MiT    AND (aPACHe-2.0 \tOR   \n gpl-2.0+)") == "MIT AND (Apache-2.0 OR GPL-2.0+)");
    CHECK(test_serialized_license("uNkNoWnLiCeNsE") == "uNkNoWnLiCeNsE");
}

TEST_CASE ("license error messages", "[manifests][license]")
{
    ParseMessages messages;
    parse_spdx_license_expression("", messages);
    REQUIRE(messages.error);
    CHECK(messages.error->format() == R"(<license string>:1:1: error: SPDX license expression was empty.
    on expression: 
                   ^
)");

    parse_spdx_license_expression("MIT ()", messages);
    REQUIRE(messages.error);
    CHECK(messages.error->format() ==
          R"(<license string>:1:5: error: Expected a compound or the end of the string, found a parenthesis.
    on expression: MIT ()
                       ^
)");

    parse_spdx_license_expression("MIT +", messages);
    REQUIRE(messages.error);
    CHECK(
        messages.error->format() ==
        R"(<license string>:1:5: error: SPDX license expression contains an extra '+'. These are only allowed directly after a license identifier.
    on expression: MIT +
                       ^
)");

    parse_spdx_license_expression("MIT AND", messages);
    REQUIRE(messages.error);
    CHECK(messages.error->format() ==
          R"(<license string>:1:8: error: Expected a license name, found the end of the string.
    on expression: MIT AND
                         ^
)");

    parse_spdx_license_expression("MIT AND unknownlicense", messages);
    CHECK(!messages.error);
    REQUIRE(messages.warnings.size() == 1);
    CHECK(
        test_format_parse_warning(messages.warnings[0]) ==
        R"(<license string>:1:9: warning: Unknown license identifier 'unknownlicense'. Known values are listed at https://spdx.org/licenses/
    on expression: MIT AND unknownlicense
                           ^)");
}
