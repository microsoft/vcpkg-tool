#include <catch2/catch.hpp>

#include <vcpkg/base/json.h>
#include <vcpkg/base/util.h>

#include <vcpkg/paragraphs.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <vcpkg-test/util.h>

#if defined(_MSC_VER)
#pragma warning(disable : 6237)
#endif

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
        Checks::exit_with_message(VCPKG_LINE_INFO, json.error()->format());
    }
}

static Parse::ParseExpected<SourceControlFile> test_parse_manifest(StringView sv, bool expect_fail = false)
{
    auto object = parse_json_object(sv);
    auto res = SourceControlFile::parse_manifest_object("<test manifest>", object);
    if (!res.has_value() && !expect_fail)
    {
        print_error_message(res.error());
    }
    REQUIRE(res.has_value() == !expect_fail);
    return res;
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
    REQUIRE(pgh.core_paragraph->version == "1.2.8");
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
    std::tuple<StringLiteral, Versions::Scheme, StringLiteral> data[] = {
        {R"json({
    "name": "zlib",
    "version-string": "abcd"
}
)json",
         Versions::Scheme::String,
         "abcd"},
        {R"json({
    "name": "zlib",
    "version-date": "2020-01-01"
}
)json",
         Versions::Scheme::Date,
         "2020-01-01"},
        {R"json({
    "name": "zlib",
    "version": "1.2.3.4.5"
}
)json",
         Versions::Scheme::Relaxed,
         "1.2.3.4.5"},
        {R"json({
    "name": "zlib",
    "version-semver": "1.2.3-rc3"
}
)json",
         Versions::Scheme::Semver,
         "1.2.3-rc3"},
    };
    for (auto&& v : data)
    {
        auto m_pgh = test_parse_manifest(std::get<0>(v));
        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        CHECK(Json::stringify(serialize_manifest(pgh), Json::JsonStyle::with_spaces(4)) == std::get<0>(v));
        CHECK(pgh.core_paragraph->version_scheme == std::get<1>(v));
        CHECK(pgh.core_paragraph->version == std::get<2>(v));
        CHECK(pgh.core_paragraph->port_version == 0);
    }

    test_parse_manifest(R"json({
        "name": "zlib",
        "version-string": "abcd",
        "version-semver": "1.2.3-rc3"
    })json",
                        true);

    test_parse_manifest(R"json({
        "name": "zlib",
        "version-string": "abcd#1"
    })json",
                        true);
    test_parse_manifest(R"json({
        "name": "zlib",
        "version": "abcd#1"
    })json",
                        true);
    test_parse_manifest(R"json({
        "name": "zlib",
        "version-date": "abcd#1"
    })json",
                        true);
    test_parse_manifest(R"json({
        "name": "zlib",
        "version-semver": "abcd#1"
    })json",
                        true);

    SECTION ("version syntax")
    {
        test_parse_manifest(R"json({
        "name": "zlib",
        "version-semver": "2020-01-01"
    })json",
                            true);
        test_parse_manifest(R"json({
        "name": "zlib",
        "version-date": "1.1.1"
    })json",
                            true);
        test_parse_manifest(R"json({
        "name": "zlib",
        "version": "1.2.3-rc3"
    })json",
                            false);
    }
}

TEST_CASE ("manifest constraints hash", "[manifests]")
{
    auto p = unwrap(test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "dependencies": [
        {
            "name": "d",
            "version>=": "2018-09-01#1"
        }
    ]
})json"));
    REQUIRE(p->core_paragraph->dependencies.at(0).constraint.value == "2018-09-01");
    REQUIRE(p->core_paragraph->dependencies.at(0).constraint.port_version == 1);

    test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "dependencies": [
        {
            "name": "d",
            "version>=": "2018-09-01#0"
        }
    ]
})json",
                        true);

    test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "dependencies": [
        {
            "name": "d",
            "version>=": "2018-09-01#-1"
        }
    ]
})json",
                        true);

    test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "dependencies": [
        {
            "name": "d",
            "version>=": "2018-09-01",
            "port-version": 1
        }
    ]
})json",
                        true);
}

TEST_CASE ("manifest overrides embedded port version", "[manifests]")
{
    test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-string": "abcd#1",
            "port-version": 1
        }
    ]
})json",
                        true);
    test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-date": "2018-01-01#1",
            "port-version": 1
        }
    ]
})json",
                        true);
    test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version": "1.2#1",
            "port-version": 1
        }
    ]
})json",
                        true);
    test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-semver": "1.2.0#1",
            "port-version": 1
        }
    ]
})json",
                        true);

    CHECK(unwrap(test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-string": "abcd#1"
        }
    ]
})json",
                                     false))
              ->core_paragraph->overrides.at(0)
              .port_version == 1);
    CHECK(unwrap(test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-date": "2018-01-01#1"
        }
    ]
})json",
                                     false))
              ->core_paragraph->overrides.at(0)
              .port_version == 1);
    CHECK(unwrap(test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version": "1.2#1"
        }
    ]
})json",
                                     false))
              ->core_paragraph->overrides.at(0)
              .port_version == 1);
    CHECK(unwrap(test_parse_manifest(R"json({
    "name": "zlib",
    "version-string": "abcd",
    "overrides": [
        {
            "name": "d",
            "version-semver": "1.2.0#1"
        }
    ]
})json",
                                     false))
              ->core_paragraph->overrides.at(0)
              .port_version == 1);
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
    REQUIRE(pgh.core_paragraph->dependencies[0].constraint ==
            DependencyConstraint{Versions::Constraint::Type::None, "", 0});
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "c");
    REQUIRE(pgh.core_paragraph->dependencies[1].constraint ==
            DependencyConstraint{Versions::Constraint::Type::None, "", 0});
    REQUIRE(pgh.core_paragraph->dependencies[2].name == "d");
    REQUIRE(pgh.core_paragraph->dependencies[2].constraint ==
            DependencyConstraint{Versions::Constraint::Type::Minimum, "2018-09-01", 0});
    REQUIRE(pgh.core_paragraph->builtin_baseline == "089fa4de7dca22c67dcab631f618d5cd0697c8d4");

    test_parse_manifest(R"json({
        "name": "zlib",
        "version-string": "abcd",
        "dependencies": [
            {
                "name": "d",
                "port-version": 5
            }
        ]
    })json",
                        true);
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
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.type == Versions::Constraint::Type::Minimum);
        REQUIRE(pgh.core_paragraph->overrides.size() == 1);
        REQUIRE(pgh.core_paragraph->overrides[0].version_scheme == Versions::Scheme::String);
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
        REQUIRE(pgh.core_paragraph->dependencies[0].constraint.type == Versions::Constraint::Type::Minimum);
        REQUIRE(pgh.core_paragraph->overrides.size() == 1);
        REQUIRE(pgh.core_paragraph->overrides[0].version_scheme == Versions::Scheme::String);
        REQUIRE(pgh.core_paragraph->overrides[0].version == "abcd");
        REQUIRE(pgh.core_paragraph->overrides[0].port_version == 0);
        REQUIRE(!pgh.core_paragraph->builtin_baseline.has_value());
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_without_versioning));
        REQUIRE(pgh.check_against_feature_flags({}, feature_flags_with_versioning));
    }
}

TEST_CASE ("manifest overrides", "[manifests]")
{
    std::tuple<StringLiteral, Versions::Scheme, StringLiteral> data[] = {
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
         Versions::Scheme::String,
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
         Versions::Scheme::Date,
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
         Versions::Scheme::Relaxed,
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
         Versions::Scheme::Semver,
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

    test_parse_manifest(R"json({
        "name": "zlib",
        "version-string": "abcd",
        "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
        "overrides": [
        {
            "name": "abc",
            "version-semver": "1.2.3-rc3",
            "version-string": "1.2.3-rc3"
        }
    ]})json",
                        true);

    test_parse_manifest(R"json({
        "name": "zlib",
        "version-string": "abcd",
        "builtin-baseline": "089fa4de7dca22c67dcab631f618d5cd0697c8d4",
        "overrides": [
        {
            "name": "abc",
            "port-version": 5
        }
    ]})json",
                        true);

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
    REQUIRE(as_json.first.is_object());
    auto as_json_obj = as_json.first.object();
    REQUIRE(Json::stringify(serialize_manifest(pgh), Json::JsonStyle::with_spaces(4)) ==
            Json::stringify(as_json_obj, Json::JsonStyle::with_spaces(4)));

    REQUIRE(pgh.core_paragraph->builtin_baseline == "089fa4de7dca22c67dcab631f618d5cd0697c8d4");
    REQUIRE(pgh.core_paragraph->dependencies.size() == 3);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "a");
    REQUIRE(pgh.core_paragraph->dependencies[0].constraint ==
            DependencyConstraint{Versions::Constraint::Type::None, "", 0});
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "b");
    REQUIRE(pgh.core_paragraph->dependencies[1].constraint ==
            DependencyConstraint{Versions::Constraint::Type::None, "", 0});
    REQUIRE(pgh.core_paragraph->dependencies[2].name == "c");
    REQUIRE(pgh.core_paragraph->dependencies[2].constraint ==
            DependencyConstraint{Versions::Constraint::Type::Minimum, "2018-09-01", 0});

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
    auto m_pgh = test_parse_manifest(R"json({
        "name": "s",
        "version-string": "v",
        "maintainers": ["m"],
        "contacts": { "a": { "aa": "aa" } },
        "summary": "d",
        "description": "d",
        "dependencies": ["bd"],
        "default-features": ["df"],
        "features": {
            "iroh" : {
                "description": "zuko's uncle",
                "dependencies": [
                    "firebending",
                    {
                        "name": "tea"
                    },
                    {
                        "name": "order.white-lotus",
                        "features": [ "the-ancient-ways" ],
                        "platform": "!(windows & arm)"
                    }
                ]
            },
            "zuko": {
                "description": ["son of the fire lord", "firebending 師父"],
                "supports": "!(windows & arm)"
            }
        }
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->name == "s");
    REQUIRE(pgh.core_paragraph->version == "v");
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

    REQUIRE(!pgh.check_against_feature_flags({}, feature_flags_without_versioning));
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
    REQUIRE(pgh.core_paragraph->version == "1.2.8");
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
    REQUIRE(pgh.core_paragraph->version == "1.2.8");
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
    auto m_pgh = test_parse_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "supports": ""
    })json",
                                     true);
    REQUIRE_FALSE(m_pgh.has_value());
}

TEST_CASE ("SourceParagraph manifest non-string supports", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "supports": true
    })json",
                                     true);
    REQUIRE_FALSE(m_pgh.has_value());
}

TEST_CASE ("Serialize all the ports", "[manifests]")
{
    std::vector<std::string> args_list = {"format-manifest"};
    auto& fs = get_real_filesystem();
    auto args = VcpkgCmdArguments::create_from_arg_sequence(args_list.data(), args_list.data() + args_list.size());
    args.imbue_from_environment();
    VcpkgPaths paths{fs, args};

    std::vector<SourceControlFile> scfs;

    for (auto&& dir : fs.get_directories_non_recursive(paths.builtin_ports_directory(), VCPKG_LINE_INFO))
    {
        const auto control = dir / "CONTROL";
        const auto manifest = dir / "vcpkg.json";
        if (fs.exists(control, IgnoreErrors{}))
        {
            INFO(control.native());
            auto contents = fs.read_contents(control, VCPKG_LINE_INFO);
            auto pghs = Paragraphs::parse_paragraphs(contents, control);
            REQUIRE(pghs);

            auto scf = SourceControlFile::parse_control_file(control, std::move(pghs).value_or_exit(VCPKG_LINE_INFO));
            if (!scf)
            {
                INFO(scf.error()->name);
                INFO(scf.error()->error);
                REQUIRE(scf);
            }

            scfs.push_back(std::move(*scf.value_or_exit(VCPKG_LINE_INFO)));
        }
        else if (fs.exists(manifest, IgnoreErrors{}))
        {
            std::error_code ec;
            auto contents = Json::parse_file(fs, manifest, ec);
            REQUIRE_FALSE(ec);
            REQUIRE(contents);

            auto scf = SourceControlFile::parse_manifest_object(manifest,
                                                                contents.value_or_exit(VCPKG_LINE_INFO).first.object());
            REQUIRE(scf);

            scfs.push_back(std::move(*scf.value_or_exit(VCPKG_LINE_INFO)));
        }
    }

    for (auto& scf : scfs)
    {
        auto serialized = serialize_manifest(scf);
        auto serialized_scf = SourceControlFile::parse_manifest_object({}, serialized).value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(*serialized_scf == scf);
    }
}
