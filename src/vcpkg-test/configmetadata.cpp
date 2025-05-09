#include <vcpkg-test/util.h>

#include <vcpkg/base/json.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/configuration.h>

using namespace vcpkg;

static constexpr StringLiteral KIND = "kind";
static constexpr StringLiteral REPOSITORY = "repository";
static constexpr StringLiteral PATH = "path";
static constexpr StringLiteral BASELINE = "baseline";
static constexpr StringLiteral NAME = "name";
static constexpr StringLiteral LOCATION = "location";
static constexpr StringLiteral CE_MESSAGE = "message";
static constexpr StringLiteral CE_WARNING = "warning";
static constexpr StringLiteral CE_ERROR = "error";
static constexpr StringLiteral CE_SETTINGS = "settings";
static constexpr StringLiteral CE_APPLY = "apply";
static constexpr StringLiteral CE_REQUIRES = "requires";
static constexpr StringLiteral CE_DEMANDS = "demands";

static void CHECK_LINES(const std::string& a, const std::string& b)
{
    auto as = Strings::split(a, '\n');
    auto bs = Strings::split(b, '\n');
    for (size_t i = 0; i < as.size() && i < bs.size(); ++i)
    {
        INFO(i);
        CHECK(as[i] == bs[i]);
    }
    CHECK(as.size() == bs.size());
}

static Configuration parse_test_configuration(StringView text)
{
    static constexpr StringLiteral origin = "test";
    auto object = Json::parse_object(text, origin).value_or_exit(VCPKG_LINE_INFO);

    Json::Reader reader(origin);
    auto parsed_config_opt = configuration_deserializer.visit(reader, object);
    REQUIRE(reader.messages().lines().empty());

    return std::move(parsed_config_opt).value_or_exit(VCPKG_LINE_INFO);
}

static void check_string(const Json::Object& obj, StringView key, StringView expected)
{
    REQUIRE(obj.contains(key));
    auto value = obj.get(key);
    REQUIRE(value->is_string());
    REQUIRE(value->string(VCPKG_LINE_INFO) == expected);
}

static void check_errors(const std::string& config_text, const std::string& expected_errors)
{
    static constexpr StringLiteral origin = "test";
    auto object = Json::parse_object(config_text, origin).value_or_exit(VCPKG_LINE_INFO);

    Json::Reader reader(origin);
    auto parsed_config_opt = configuration_deserializer.visit(reader, object);
    CHECK_LINES(reader.messages().join().data(), expected_errors);
}

TEST_CASE ("config registries only", "[ce-metadata]")
{
    SECTION ("valid json")
    {
        std::string raw_config = R"json({
    "default-registry": {
        "kind": "builtin",
        "baseline": "843e0ba0d8f9c9c572e45564263eedfc7745e74f"
    },
    "registries": [
        {
            "kind": "git",
            "repository": "https://github.com/northwindtraders/vcpkg-registry",
            "baseline": "dacf4de488094a384ca2c202b923ccc097956e0c",
            "packages": [ "beicode", "beison" ]
        },
        {
            "kind": "filesystem",
            "path": "path/to/registry",
            "packages": [ "zlib" ]
        },
        {
            "kind": "artifact",
            "name": "vcpkg-artifacts",
            "location": "https://github.com/microsoft/vcpkg-artifacts"
        },
        {
            "kind": "filesystem",
            "path": "path/to/registry",
            "packages": [ ]
        }
    ]
})json";

        auto config = parse_test_configuration(raw_config);
        REQUIRE(config.ce_metadata.is_empty());
        REQUIRE(config.extra_info.is_empty());

        REQUIRE(config.default_reg.has_value());

        auto default_registry = config.default_reg.get()->serialize().object(VCPKG_LINE_INFO);
        check_string(default_registry, KIND, "builtin");
        check_string(default_registry, BASELINE, "843e0ba0d8f9c9c572e45564263eedfc7745e74f");

        REQUIRE(config.registries.size() == 4);

        const auto& git_registry = config.registries[0];
        auto serialized_git_registry = git_registry.serialize().object(VCPKG_LINE_INFO);
        check_string(serialized_git_registry, KIND, "git");
        check_string(serialized_git_registry, REPOSITORY, "https://github.com/northwindtraders/vcpkg-registry");
        check_string(serialized_git_registry, BASELINE, "dacf4de488094a384ca2c202b923ccc097956e0c");
        REQUIRE(git_registry.packages);
        auto&& p = *git_registry.packages.get();
        REQUIRE(p.size() == 2);
        REQUIRE(p[0] == "beicode");
        REQUIRE(p[1] == "beison");

        const auto& fs_registry = config.registries[1];
        auto serialized_fs_registry = fs_registry.serialize().object(VCPKG_LINE_INFO);
        check_string(serialized_fs_registry, KIND, "filesystem");
        check_string(serialized_fs_registry, PATH, "path/to/registry");
        REQUIRE(fs_registry.packages);
        REQUIRE(fs_registry.packages.get()->size() == 1);
        REQUIRE((*fs_registry.packages.get())[0] == "zlib");

        const auto& artifact_registry = config.registries[2];
        auto serialized_art_registry = artifact_registry.serialize().object(VCPKG_LINE_INFO);
        check_string(serialized_art_registry, KIND, "artifact");
        check_string(serialized_art_registry, NAME, "vcpkg-artifacts");
        check_string(serialized_art_registry, LOCATION, "https://github.com/microsoft/vcpkg-artifacts");
        REQUIRE(!artifact_registry.packages);

        REQUIRE(config.registries[3].packages);

        auto raw_obj = Json::parse_object(raw_config, "test").value_or_exit(VCPKG_LINE_INFO);
        auto serialized_obj = config.serialize();
        Test::check_json_eq(raw_obj, serialized_obj);
    }

    SECTION ("default invalid json")
    {
        std::string raw_no_baseline = R"json({
    "default-registry": {
        "kind": "builtin"
    }
})json";
        check_errors(raw_no_baseline, R"(
test: error: $.default-registry (a builtin registry): missing required field 'baseline' (a baseline)
)");

        std::string raw_with_packages = R"json({
    "default-registry": {
        "kind": "builtin",
        "baseline": "aaaaabbbbbcccccdddddeeeeefffff0000011111",
        "packages": [ "zlib", "boost" ]
    }
})json";
        check_errors(raw_with_packages, R"(
test: error: $.default-registry (a registry): unexpected field 'packages', did you mean 'path'?
)");

        std::string raw_default_artifact = R"json({
    "default-registry": {
        "kind": "artifact",
        "name": "default-artifacts",
        "location": "https://github.com/microsoft/vcpkg-artifacts"
    }
})json";
        check_errors(raw_default_artifact, R"(
test: error: $ (a configuration object): The default registry cannot be an artifact registry.
)");
        std::string raw_bad_kind = R"json({
    "registries": [{
        "kind": "custom"
    }]
})json";
        check_errors(raw_bad_kind, R"(
test: error: $.registries[0] (a registry): "kind" did not have an expected value: (expected one of: builtin, filesystem, git, artifact; found custom)
test: error: $.registries[0]: mismatched type: expected a registry
)");

        std::string raw_bad_fs_registry = R"json({
    "registries": [{
        "kind": "filesystem",
        "path": "D:/microsoft/vcpkg",
        "baseline": "default",
        "reference": "main"
    }]
})json";
        check_errors(raw_bad_fs_registry, R"(
test: error: $.registries[0] (a filesystem registry): unexpected field 'reference', did you mean 'baseline'?
test: error: $.registries[0] (a registry): missing required field 'packages' (a package pattern array)
)");

        std::string raw_bad_git_registry = R"json({
    "registries": [{
        "kind": "git",
        "no-repository": "https://github.com/microsoft/vcpkg",
        "baseline": "abcdef",
        "reference": {},
        "packages": {}
    }]
})json";
        check_errors(raw_bad_git_registry, R"(
test: error: $.registries[0] (a registry): unexpected field 'no-repository', did you mean 'repository'?
test: error: $.registries[0] (a git registry): missing required field 'repository' (a git repository URL)
test: error: $.registries[0].reference: mismatched type: expected a git reference (for example, a branch)
test: error: $.registries[0] (a git registry): unexpected field 'no-repository', did you mean 'repository'?
test: error: $.registries[0].packages: mismatched type: expected a package pattern array
)");

        std::string raw_bad_artifact_registry = R"json({
    "registries": [{
        "kind": "artifact",
        "no-location": "https://github.com/microsoft/vcpkg",
        "baseline": "1234567812345678123456781234567812345678",
        "packages": [ "zlib" ]
    }]
})json";
        check_errors(raw_bad_artifact_registry, R"(
test: error: $.registries[0] (a registry): unexpected field 'no-location', did you mean 'location'?
test: error: $.registries[0] (an artifacts registry): missing required field 'name' (an identifier)
test: error: $.registries[0] (an artifacts registry): missing required field 'location' (an artifacts git registry URL)
test: error: $.registries[0] (an artifacts registry): unexpected field 'no-location', did you mean 'location'?
test: error: $.registries[0] (an artifacts registry): unexpected field 'baseline', did you mean 'kind'?
test: error: $.registries[0] (an artifacts registry): unexpected field 'packages', did you mean 'name'?
)");
    }
}

TEST_CASE ("config ce metadata only", "[ce-metadata]")
{
    std::string raw_config = R"json({
    "$comment": "this is a comment",
    "unexpected": "this is unexpected but we leave it be",
    "message": "this is a message",
    "warning": "this is a warning",
    "error": "this is an error",
    "demands": {
        "nested": {
            "$comment": "this is a comment too",
            "unexpected": "this is unexpected too",
            "message": "this is a message too",
            "warning": "this is a warning too",
            "error": "this is an error too"
        }
    }
})json";

    auto config = parse_test_configuration(raw_config);
    REQUIRE(!config.registries.size());

    REQUIRE(!config.extra_info.is_empty());
    REQUIRE(config.extra_info.size() == 1);
    check_string(config.extra_info, "$comment", "this is a comment");

    REQUIRE(!config.ce_metadata.is_empty());
    const auto& ce_metadata = config.ce_metadata;
    check_string(ce_metadata, CE_MESSAGE, "this is a message");
    check_string(ce_metadata, CE_WARNING, "this is a warning");
    check_string(ce_metadata, CE_ERROR, "this is an error");
    REQUIRE(!ce_metadata.contains("$comment"));
    REQUIRE(ce_metadata.contains("unexpected"));

    REQUIRE(ce_metadata.contains(CE_DEMANDS));
    auto demands_val = ce_metadata.get(CE_DEMANDS);
    REQUIRE(demands_val->is_object());
    const auto& demands = demands_val->object(VCPKG_LINE_INFO);
    REQUIRE(demands.size() == 1);
    REQUIRE(demands.contains("nested"));
    auto nested_val = demands.get("nested");
    REQUIRE(nested_val->is_object());
    const auto& nested = nested_val->object(VCPKG_LINE_INFO);
    check_string(nested, CE_MESSAGE, "this is a message too");
    check_string(nested, CE_WARNING, "this is a warning too");
    check_string(nested, CE_ERROR, "this is an error too");
    REQUIRE(nested.contains("$comment"));
    REQUIRE(nested.contains("unexpected"));

    auto raw_obj = Json::parse_object(raw_config, "test").value_or_exit(VCPKG_LINE_INFO);
    auto serialized_obj = config.serialize();
    Test::check_json_eq(raw_obj, serialized_obj);
}

TEST_CASE ("metadata strings", "[ce-metadata]")
{
    SECTION ("valid json")
    {
        std::string valid_raw = R"json({
    "message": "this is a valid message",
    "warning": "this is a valid warning",
    "error": "this is a valid error"
})json";

        auto valid_config = parse_test_configuration(valid_raw);
        CHECK(valid_config.ce_metadata.size() == 3);
        check_string(valid_config.ce_metadata, CE_MESSAGE, "this is a valid message");
        check_string(valid_config.ce_metadata, CE_WARNING, "this is a valid warning");
        check_string(valid_config.ce_metadata, CE_ERROR, "this is a valid error");

        auto raw_obj = Json::parse_object(valid_raw, "test").value_or_exit(VCPKG_LINE_INFO);
        Test::check_json_eq(raw_obj, valid_config.serialize());
    }

    SECTION ("invalid json")
    {
        std::string invalid_raw = R"json({
    "message": { "$comment": "this is not a valid message" },
    "warning": 0,
    "error": null
})json";

        check_errors(invalid_raw, R"(
test: error: $.error: mismatched type: expected a string
test: error: $.warning: mismatched type: expected a string
test: error: $.message: mismatched type: expected a string
)");
    }
}

TEST_CASE ("metadata dictionaries", "[ce-metadata]")
{
    SECTION ("valid json")
    {
        std::string valid_raw = R"json({
    "settings": {
        "SETTING_1": "value1",
        "SETTING_2": "value2"
    },
    "requires": {
        "fruits/a/apple": "1.0.0",
        "fruits/a/avocado": "2.0.0"
    }
})json";

        auto valid_config = parse_test_configuration(valid_raw);
        CHECK(valid_config.ce_metadata.size() == 2);

        auto requires_val = valid_config.ce_metadata.get(CE_REQUIRES);
        REQUIRE(requires_val);
        REQUIRE(requires_val->is_object());
        const auto& requires_ = requires_val->object(VCPKG_LINE_INFO);
        check_string(requires_, "fruits/a/apple", "1.0.0");
        check_string(requires_, "fruits/a/avocado", "2.0.0");

        auto settings_val = valid_config.ce_metadata.get(CE_SETTINGS);
        REQUIRE(settings_val);
        REQUIRE(settings_val->is_object());
        const auto& settings = settings_val->object(VCPKG_LINE_INFO);
        check_string(settings, "SETTING_1", "value1");
        check_string(settings, "SETTING_2", "value2");

        auto raw_obj = Json::parse_object(valid_raw, "test").value_or_exit(VCPKG_LINE_INFO);
        Test::check_json_eq(raw_obj, valid_config.serialize());
    }

    SECTION ("invalid json")
    {
        std::string invalid_raw = R"json({
    "settings": [],
    "requires": {
        "fruits/a/apple": null,
        "fruits/a/avocado": 1
    },
    "demands": {
        "nested": {
            "settings": [],
            "requires": {
                "fruits/a/apple": null,
                "fruits/a/avocado": 1
            }
        }
    }
})json";
        check_errors(invalid_raw, R"(
test: error: $ (settings): expected an object
test: error: $.requires (a "string": "string" dictionary): value of ["fruits/a/apple"] must be a string
test: error: $.requires (a "string": "string" dictionary): value of ["fruits/a/avocado"] must be a string
test: error: $.demands (settings): expected an object
test: error: $.demands.requires (a "string": "string" dictionary): value of ["fruits/a/apple"] must be a string
test: error: $.demands.requires (a "string": "string" dictionary): value of ["fruits/a/avocado"] must be a string
)");
    }
}

TEST_CASE ("metadata demands", "[ce-metadata]")
{
    SECTION ("simple demands")
    {
        std::string simple_raw = R"json({
    "demands": {
         "level0": {
            "message": "this is level 0"
         },
        "level1": {
            "message": "this is level 1"
        }
    }
})json";

        auto config = parse_test_configuration(simple_raw);
        REQUIRE(!config.ce_metadata.is_empty());
        CHECK(config.ce_metadata.size() == 1);
        auto demands_val = config.ce_metadata.get(CE_DEMANDS);
        REQUIRE(demands_val);
        REQUIRE(demands_val->is_object());
        const auto& demands = demands_val->object(VCPKG_LINE_INFO);
        CHECK(demands.size() == 2);

        auto level0_val = demands.get("level0");
        REQUIRE(level0_val);
        REQUIRE(level0_val->is_object());
        const auto& level0 = level0_val->object(VCPKG_LINE_INFO);
        CHECK(level0.size() == 1);
        check_string(level0, CE_MESSAGE, "this is level 0");

        auto level1_val = demands.get("level1");
        REQUIRE(level1_val);
        REQUIRE(level1_val->is_object());
        const auto& level1 = level1_val->object(VCPKG_LINE_INFO);
        CHECK(level1.size() == 1);
        check_string(level1, CE_MESSAGE, "this is level 1");

        auto raw_obj = Json::parse_object(simple_raw, "test").value_or_exit(VCPKG_LINE_INFO);
        Test::check_json_eq(raw_obj, config.serialize());
    }

    SECTION ("invalid json")
    {
        std::string invalid_raw = R"json({
    "demands": {
         "a": null,
         "b": [],
         "c": "string",
         "d": 12345,
         "e": false,
         "f": {
            "demands": {
                "f.1": {
                    "message": {
                        "causes-error": true
                    }
                }
            }
        }
    }
})json";
        check_errors(invalid_raw, R"(
test: error: $.demands (a demand object): value of ["a"] must be an object
test: error: $.demands (a demand object): value of ["b"] must be an object
test: error: $.demands (a demand object): value of ["c"] must be an object
test: error: $.demands (a demand object): value of ["d"] must be an object
test: error: $.demands (a demand object): value of ["e"] must be an object
test: error: $.demands (a demand object): ["f"] contains a nested `demands` object (nested `demands` have no effect)
)");
    }
}

TEST_CASE ("serialize configuration", "[ce-metadata]")
{
    SECTION ("only overlay ports")
    {
        std::string raw = R"json({
    "overlay-ports": [
		"./my-ports/fmt",
		"/custom-ports",
		"../share/team-ports",
        "my-ports/fmt"
	]
})json";
        // parsing of configuration is tested elsewhere
        auto config = parse_test_configuration(raw);
        Test::check_json_eq(Json::parse_object(raw, "test").value_or_exit(VCPKG_LINE_INFO), config.serialize());
    }

    SECTION ("invalid overlay ports")
    {
        std::string raw = R"json({
    "overlay-ports": [
		"./my-ports/fmt" ,
		"/custom-ports",
		123
	]
})json";
        check_errors(raw, R"(
test: error: $.overlay-ports[2]: mismatched type: expected an overlay path
)");
    }

    SECTION ("only overlay triplets")
    {
        std::string raw = R"json({
    "overlay-triplets": [
		"./team-triplets"
	]
})json";
        // parsing of configuration is tested elsewhere
        auto config = parse_test_configuration(raw);
        Test::check_json_eq(Json::parse_object(raw, "test").value_or_exit(VCPKG_LINE_INFO), config.serialize());
    }

    SECTION ("invalid overlay triplets")
    {
        std::string raw = R"json({
    "overlay-triplets": [
		123
	]
})json";
        check_errors(raw, R"(
test: error: $.overlay-triplets[0]: mismatched type: expected a triplet path
)");
    }

    SECTION ("both overlay ports and overlay triplets")
    {
        std::string raw = R"json({
    "overlay-ports": [
		"./my-ports/fmt" ,
		"/custom-ports",
		"../share/team-ports"
	],
    "overlay-triplets": [
		"./team-triplets"
	]
})json";
        // parsing of configuration is tested elsewhere
        auto config = parse_test_configuration(raw);
        Test::check_json_eq(Json::parse_object(raw, "test").value_or_exit(VCPKG_LINE_INFO), config.serialize());
    }

    SECTION ("overriden default registry, registries and overlays")
    {
        std::string raw = R"json({
    "default-registry": {
        "kind": "builtin",
        "baseline": "843e0ba0d8f9c9c572e45564263eedfc7745e74f"
    },
    "registries": [
        {
            "kind": "git",
            "repository": "https://github.com/microsoft/vcpkg",
            "baseline": "843e0ba0d8f9c9c572e45564263eedfc7745e74f",
            "packages": [ "zlib" ]
        }
    ],
    "overlay-ports": [
		"./my-ports/fmt" ,
		"/custom-ports",
		"../share/team-ports"
	],
    "overlay-triplets": [
		"./team-triplets"
	]
})json";
        // parsing of configuration is tested elsewhere
        auto config = parse_test_configuration(raw);
        Test::check_json_eq(Json::parse_object(raw, "test").value_or_exit(VCPKG_LINE_INFO), config.serialize());
    }

    SECTION ("null default registry")
    {
        std::string raw = R"json({
    "default-registry": null,
    "registries": [
        {
            "kind": "git",
            "repository": "https://github.com/microsoft/vcpkg",
            "baseline": "843e0ba0d8f9c9c572e45564263eedfc7745e74f",
            "packages": [ "zlib" ]
        }
    ]
})json";
        // parsing of configuration is tested elsewhere
        auto config = parse_test_configuration(raw);
        Test::check_json_eq(Json::parse_object(raw, "test").value_or_exit(VCPKG_LINE_INFO), config.serialize());
    }

    SECTION ("overriden default registry and registries")
    {
        std::string raw = R"json({
    "default-registry": {
        "kind": "builtin",
        "baseline": "843e0ba0d8f9c9c572e45564263eedfc7745e74f"
    },
    "registries": [
        {
            "kind": "git",
            "repository": "https://github.com/microsoft/vcpkg",
            "baseline": "843e0ba0d8f9c9c572e45564263eedfc7745e74f",
            "packages": [ "zlib" ]
        }
    ]
})json";
        // parsing of configuration is tested elsewhere
        auto config = parse_test_configuration(raw);
        Test::check_json_eq(Json::parse_object(raw, "test").value_or_exit(VCPKG_LINE_INFO), config.serialize());
    }

    SECTION ("only registries")
    {
        std::string raw = R"json({
    "registries": [
        {
            "kind": "git",
            "repository": "https://github.com/microsoft/vcpkg",
            "baseline": "843e0ba0d8f9c9c572e45564263eedfc7745e74f",
            "packages": [ "zlib" ]
        }
    ]
})json";
        // parsing of configuration is tested elsewhere
        auto config = parse_test_configuration(raw);
        Test::check_json_eq(Json::parse_object(raw, "test").value_or_exit(VCPKG_LINE_INFO), config.serialize());
    }

    SECTION ("preserve comments and unexpected fields")
    {
        std::string raw = R"json({
    "$comment1": "aaaaah",
    "$comment2": "aaaaaaaaaah",
    "$comment3": "aaaaaaaaaaaaaaaaaaah",
    "unexpected": true,
    "unexpected-too": "yes",
    "demands": {
        "$comment object": [],
        "comments": {
           "$comment4": "aaaaaaaaaaaaaaaaaaaaaaaaaaaah",
           "hello": "world",
           "hola": "mundo"
        },
        "$another comment object": {
            "ignored-unknown": "because is inside a comment"
        }
    }
})json";

        auto config = parse_test_configuration(raw);
        Test::check_json_eq(Json::parse_object(raw, "test").value_or_exit(VCPKG_LINE_INFO), config.serialize());

        auto extra_fields = find_unknown_fields(config);
        CHECK(extra_fields.size() == 4);
        REQUIRE(extra_fields[0] == "$.unexpected");
        REQUIRE(extra_fields[1] == "$.unexpected-too");
        REQUIRE(extra_fields[2] == "$.demands.comments.hello");
        REQUIRE(extra_fields[3] == "$.demands.comments.hola");
    }

    SECTION ("sorted fields")
    {
        std::string raw = R"json({
    "registries": [
        {
            "baseline": "843e0ba0d8f9c9c572e45564263eedfc7745e74f",
            "repository": "https://github.com/microsoft/vcpkg",
            "kind": "git",
            "packages": [ "zlib" ]
        }
    ],
    "default-registry": null,
    "error": "this is an error",
    "message": "this is a message",
    "warning": "this is a warning",
    "$comment": "this is a comment",
    "unexpected": "this is an unexpected field",
    "$comment2": "this is another comment",
    "demands": {
        "a": {
            "error": "nested error",
            "$comment": "nested comment",
            "message": "nested message",
            "unexpected": "nested unexpected"
        }
    },
    "apply": {},
    "requires": {
        "b": "banana"
    },
    "settings": {
        "a": "apple"
    }
})json";

        std::string formatted = R"json({
    "$comment": "this is a comment",
    "$comment2": "this is another comment",
    "default-registry": null,
    "registries": [
        {
            "kind": "git",
            "repository": "https://github.com/microsoft/vcpkg",
            "baseline": "843e0ba0d8f9c9c572e45564263eedfc7745e74f",
            "packages": [
                "zlib"
            ]
        }
    ],
    "unexpected": "this is an unexpected field",
    "message": "this is a message",
    "warning": "this is a warning",
    "error": "this is an error",
    "settings": {
        "a": "apple"
    },
    "apply": {},
    "requires": {
        "b": "banana"
    },
    "demands": {
        "a": {
            "$comment": "nested comment",
            "unexpected": "nested unexpected",
            "message": "nested message",
            "error": "nested error"
        }
    }
})json";

        // This test ensures the following order after serialization:
        //   comments,
        //   default-registry,
        //   registries,
        //   unexpected fields,
        //   message,
        //   warninng,
        //   error,
        //   settings,
        //   apply,
        //   requires,
        //   demands
        // Object values in `demands` are also sorted recursively.
        auto config = parse_test_configuration(raw);
        Test::check_json_eq(Json::parse_object(formatted, "test").value_or_exit(VCPKG_LINE_INFO), config.serialize());
    }
}

TEST_CASE ("config with ce metadata full example", "[ce-metadata]")
{
    std::string ce_config_section = R"json(
    "unexpected": "this goes in ce_metadata",
    "message": "this is a message",
    "warning": "this is a warning",
    "error": "this is an error",
    "settings": {
        "VCPKG_ROOT": "C:/Users/viromer/work/vcpkg",
        "VCPKG_TARGET_TRIPLET": "arm-windows"
    },
    "apply": {
        "key": "value",
        "complex-key": { "a": "apple", "b": "banana" }
    },
    "requires": {
        "tools/kitware/cmake": ">=3.21.0"
    },
    "demands": {
        "windows and target:arm": {
            "$comment": "this is a comment",
            "unexpected": "this field does nothing",
            "null": null,
            "number": 2,
            "message": "this is a nested message",
            "warning": "this is a nested warning",
            "error": "this is a nested error",
            "requires": {
                "compilers/microsoft/msvc/arm": "~17.0.0"
            }
        },
        "$ignore-errors": {
            "error": { "this would have caused a parser error": null },
            "message": "this would have been ok",
            "requires": null,
            "what-is-this": null,
            "$comment": "this fields won't be reordered at all"
        }
    }
)json";

    std::string raw_config = Strings::concat(R"json({
    "$comment": "this goes in extra_info",
    "$comment2": "this is a second comment",
    "default-registry": {
        "kind": "builtin",
        "baseline": "843e0ba0d8f9c9c572e45564263eedfc7745e74f"
    },
    "registries": [
        {
            "kind": "git",
            "repository": "https://github.com/northwindtraders/vcpkg-registry",
            "baseline": "dacf4de488094a384ca2c202b923ccc097956e0c",
            "packages": [ "beicode", "beison" ]
        }
    ],
)json",
                                             ce_config_section,
                                             "}");

    auto config = parse_test_configuration(raw_config);
    REQUIRE(config.default_reg.has_value());

    auto default_registry = config.default_reg.get()->serialize().object(VCPKG_LINE_INFO);
    check_string(default_registry, KIND, "builtin");
    check_string(default_registry, BASELINE, "843e0ba0d8f9c9c572e45564263eedfc7745e74f");

    REQUIRE(config.registries.size() == 1);
    const auto& registry = *config.registries.begin();
    auto serialized_registry = registry.serialize().object(VCPKG_LINE_INFO);
    check_string(serialized_registry, KIND, "git");
    check_string(serialized_registry, REPOSITORY, "https://github.com/northwindtraders/vcpkg-registry");
    check_string(serialized_registry, BASELINE, "dacf4de488094a384ca2c202b923ccc097956e0c");
    REQUIRE(registry.packages);
    REQUIRE(registry.packages.get()->size() == 2);
    REQUIRE(registry.packages.get()->at(0) == "beicode");
    REQUIRE(registry.packages.get()->at(1) == "beison");

    REQUIRE(!config.extra_info.is_empty());
    REQUIRE(config.extra_info.size() == 2);
    check_string(config.extra_info, "$comment", "this goes in extra_info");
    check_string(config.extra_info, "$comment2", "this is a second comment");

    REQUIRE(!config.ce_metadata.is_empty());
    const auto& ce_metadata = config.ce_metadata;

    /*
    "$comment": "this goes in extra_info",
    "$comment2": "this is a second comment",
    "unexpected": "this goes in ce_metadata",
    "message": "this is a message",
    "warning": "this is a warning",
    "error": "this is an error"
    */
    REQUIRE(!ce_metadata.contains("$comment"));
    REQUIRE(!ce_metadata.contains("$comment2"));
    check_string(ce_metadata, "unexpected", "this goes in ce_metadata");
    check_string(ce_metadata, CE_MESSAGE, "this is a message");
    check_string(ce_metadata, CE_WARNING, "this is a warning");
    check_string(ce_metadata, CE_ERROR, "this is an error");

    /*
    "settings": {
      "VCPKG_ROOT": "C:/Users/viromer/work/vcpkg",
      "VCPKG_TARGET_TRIPLET": "arm-windows"
    }
    */
    REQUIRE(ce_metadata.contains(CE_SETTINGS));
    auto settings_val = ce_metadata.get(CE_SETTINGS);
    REQUIRE(settings_val->is_object());
    const auto& settings = settings_val->object(VCPKG_LINE_INFO);
    REQUIRE(settings.size() == 2);
    check_string(settings, "VCPKG_ROOT", "C:/Users/viromer/work/vcpkg");
    check_string(settings, "VCPKG_TARGET_TRIPLET", "arm-windows");

    /*
    "apply":{
      "key": "value",
      "complex-key": {"a" : "apple", "b" : "banana"}
    }
    */
    REQUIRE(ce_metadata.contains(CE_APPLY));
    auto apply_val = ce_metadata.get(CE_APPLY);
    REQUIRE(apply_val->is_object());
    const auto& apply = apply_val->object(VCPKG_LINE_INFO);
    REQUIRE(apply.size() == 2);
    check_string(apply, "key", "value");
    auto apply_complex_key_val = apply.get("complex-key");
    REQUIRE(apply_complex_key_val->is_object());
    const auto& apply_complex_key = apply_complex_key_val->object(VCPKG_LINE_INFO);
    REQUIRE(apply_complex_key.size() == 2);
    check_string(apply_complex_key, "a", "apple");
    check_string(apply_complex_key, "b", "banana");

    /*
    "requires": {
      "tools/kitware/cmake": ">=3.21.0"
    }
    */
    REQUIRE(ce_metadata.contains(CE_REQUIRES));
    auto requires_val = ce_metadata.get(CE_REQUIRES);
    REQUIRE(requires_val->is_object());
    const auto& requires_ = requires_val->object(VCPKG_LINE_INFO);
    REQUIRE(requires_.size() == 1);
    check_string(requires_, "tools/kitware/cmake", ">=3.21.0");

    /*
    "demands": {
      "windows and target:arm": {
        "$comment": "this is a comment",
        "unexpected": "this field does nothing",
        "message": "this is a nested message",
        "warning": "this is a nested warning",
        "error": "this is a nested error"
        "requires": {
          "compilers/microsoft/msvc/arm": "~17.0.0"
        }
      },
    */
    REQUIRE(ce_metadata.contains(CE_DEMANDS));
    auto demands_val = ce_metadata.get(CE_DEMANDS);
    REQUIRE(demands_val->is_object());
    const auto& demands = demands_val->object(VCPKG_LINE_INFO);
    REQUIRE(demands.size() == 2);

    REQUIRE(demands.contains("windows and target:arm"));
    auto demand1_val = demands.get("windows and target:arm");
    REQUIRE(demand1_val->is_object());
    const auto& demand1 = demand1_val->object(VCPKG_LINE_INFO);
    REQUIRE(demand1.size() == 8);
    check_string(demand1, "$comment", "this is a comment");
    check_string(demand1, "unexpected", "this field does nothing");
    REQUIRE(demand1.get("null")->is_null());
    REQUIRE(demand1.get("number")->number(VCPKG_LINE_INFO) == 2);
    check_string(demand1, "message", "this is a nested message");
    check_string(demand1, "warning", "this is a nested warning");
    check_string(demand1, "error", "this is a nested error");
    REQUIRE(demand1.contains(CE_REQUIRES));
    auto demand1_requires_val = demand1.get(CE_REQUIRES);
    REQUIRE(demand1_requires_val->is_object());
    const auto& demand1_requires = demand1_requires_val->object(VCPKG_LINE_INFO);
    REQUIRE(demand1_requires.size() == 1);
    check_string(demand1_requires, "compilers/microsoft/msvc/arm", "~17.0.0");

    /*
      "$ignore-errors": {
        "error": { "this would have caused a parser error": null },
        "message": "this would have been ok",
        "requires": null,
        "what-is-this": null,
        "$comment": "this fields won't be reordered at all"
      },
    */
    REQUIRE(demands.contains("$ignore-errors"));
    auto demand2_val = demands.get("$ignore-errors");
    REQUIRE(demand2_val->is_object());
    const auto& demand2 = demand2_val->object(VCPKG_LINE_INFO);
    REQUIRE(demand2.size() == 5);
    REQUIRE(demand2.contains(CE_ERROR));
    auto demand2_error_val = demand2.get(CE_ERROR);
    REQUIRE(demand2_error_val->is_object());
    const auto& demand2_error = demand2_error_val->object(VCPKG_LINE_INFO);
    REQUIRE(demand2_error.contains("this would have caused a parser error"));
    REQUIRE(demand2_error.get("this would have caused a parser error")->is_null());
    check_string(demand2, "message", "this would have been ok");
    REQUIRE(demand2.contains(CE_REQUIRES));
    REQUIRE(demand2.get(CE_REQUIRES)->is_null());
    REQUIRE(demand2.contains("what-is-this"));
    REQUIRE(demand2.get("what-is-this")->is_null());
    REQUIRE(demand2.contains("$comment"));
    check_string(demand2, "$comment", "this fields won't be reordered at all");

    // finally test serialization is OK
    auto raw_obj = Json::parse_object(raw_config, "test").value_or_exit(VCPKG_LINE_INFO);
    auto serialized_obj = config.serialize();
    Test::check_json_eq(raw_obj, serialized_obj);
}
