#include <catch2/catch.hpp>

#include <vcpkg/base/json.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/configuration.h>

#include <vcpkg-test/util.h>

using namespace vcpkg;

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

static Configuration test_parse_configuration(StringView text, bool expect_fail = false)
{
    auto object = parse_json_object(text);

    Json::Reader reader;
    auto deserializer = make_configuration_deserializer("test");
    auto parsed_config_opt = reader.visit(object, *deserializer);
    CHECK((!expect_fail && reader.errors().empty()));

    return std::move(parsed_config_opt).value_or_exit(VCPKG_LINE_INFO);
}

TEST_CASE ("no ce metadata", "[ce-configuration]")
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
        }
    ]
})json";

    auto config = test_parse_configuration(raw_config);
    auto config_obj = serialize_configuration(config);
    auto raw_config_obj = parse_json_object(raw_config);

    CHECK(config.ce_metadata.is_empty());
    REQUIRE(Json::stringify(config_obj, Json::JsonStyle::with_spaces(4)) ==
            Json::stringify(raw_config_obj, Json::JsonStyle::with_spaces(4)));
}

TEST_CASE ("parse config with ce metadata", "[ce-configuration]")
{
    std::string ce_config_section = R"json(
    "error": "this is an error",
    "warning": "this is a warning",
    "message": "this is a message",
    "apply": {
        "key": "value",
        "complex-key": { "a": "apple", "b": "banana" }
    },
    "settings": {
        "VCPKG_ROOT": "C:/Users/viromer/work/vcpkg",
        "VCPKG_TARGET_TRIPLET": "arm-windows"
    },
    "requires": {
        "tools/kitware/cmake": ">=3.21.0"
    },
    "see-also": {
        "tools/ninja-build/ninja": "1.10.2"
    },
    "demands": {
        "windows and target:arm": {
            "requires": {
                "compilers/microsoft/msvc/arm": "~17.0.0"
            }
        },
        "linux and target:arm": {
            "requires": {
                "compilers/gnu/clang": "9.11.0"
            },
            "see-also": {}
        },
        "messages": {
            "error": "this is a nested error",
            "warning": "this is a nested warning",
            "message": "this is a nested message"
        },
        "$with-errors": {
            "error": { "this would have caused a parser error": null },
            "message": "this is ok",
            "requires": null,
            "what-is-this": null
        }
    }
)json";

    std::string raw_config = Strings::concat(R"json({
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

    auto config = test_parse_configuration(raw_config);
    auto config_obj = serialize_configuration(config);
    auto raw_config_obj = parse_json_object(raw_config);
    const auto& parsed_ce_config = config.ce_metadata;

    CHECK(!parsed_ce_config.is_empty());
    REQUIRE(Json::stringify(config_obj, Json::JsonStyle::with_spaces(4)) ==
            Json::stringify(raw_config_obj, Json::JsonStyle::with_spaces(4)));

    auto check_string_value = [](const Json::Object& obj, StringView key, StringView expected) {
        CHECK(obj.contains(key));
        auto value = *obj.get(key);
        CHECK(value.is_string());
        REQUIRE(value.string() == expected);
    };

    check_string_value(parsed_ce_config, "error", "this is an error");
    check_string_value(parsed_ce_config, "warning", "this is a warning");
    check_string_value(parsed_ce_config, "message", "this is a message");
}
