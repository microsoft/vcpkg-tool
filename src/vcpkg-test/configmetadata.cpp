#include <catch2/catch.hpp>

#include <vcpkg/base/json.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/configuration.h>
#include <vcpkg/registries.h>

#include <vcpkg-test/util.h>

using namespace vcpkg;

static constexpr StringLiteral DEFAULT_REGISTRY = "default-registry";
static constexpr StringLiteral REGISTRIES = "registries";
static constexpr StringLiteral KIND = "kind";
static constexpr StringLiteral REPOSITORY = "repository";
static constexpr StringLiteral PATH = "path";
static constexpr StringLiteral BASELINE = "baseline";
static constexpr StringLiteral CE_MESSAGE = "message";
static constexpr StringLiteral CE_WARNING = "warning";
static constexpr StringLiteral CE_ERROR = "error";
static constexpr StringLiteral CE_SETTINGS = "settings";
static constexpr StringLiteral CE_APPLY = "apply";
static constexpr StringLiteral CE_REQUIRES = "requires";
static constexpr StringLiteral CE_SEE_ALSO = "see-also";
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

static Configuration parse_test_configuration(StringView text)
{
    auto object = parse_json_object(text);

    Json::Reader reader;
    auto deserializer = make_configuration_deserializer("test");
    auto parsed_config_opt = reader.visit(object, *deserializer);
    REQUIRE(reader.errors().empty());

    return std::move(parsed_config_opt).value_or_exit(VCPKG_LINE_INFO);
}

static void check_string(const Json::Object& obj, StringView key, StringView expected)
{
    REQUIRE(obj.contains(key));
    auto value = obj.get(key);
    REQUIRE(value->is_string());
    REQUIRE(value->string() == expected);
}

static void compare_json_objects(const Json::Object& expected, const Json::Object& actual)
{
    REQUIRE(Json::stringify(expected, Json::JsonStyle::with_spaces(4)) ==
            Json::stringify(actual, Json::JsonStyle::with_spaces(4)));
}

TEST_CASE ("config without ce metadata", "[ce-metadata]")
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

    auto config = parse_test_configuration(raw_config);
    REQUIRE(config.ce_metadata.is_empty());
    REQUIRE(config.extra_info.is_empty());
    REQUIRE(config.registry_set.default_registry() != nullptr);

    auto default_registry = std::move(config.registry_set.default_registry()->serialize());
    check_string(default_registry, KIND, "builtin");
    check_string(default_registry, BASELINE, "843e0ba0d8f9c9c572e45564263eedfc7745e74f");

    REQUIRE(config.registry_set.registries().size() == 1);
    const auto& registry = *config.registry_set.registries().begin();
    auto serialized_registry = std::move(registry.implementation().serialize());
    check_string(serialized_registry, KIND, "git");
    check_string(serialized_registry, REPOSITORY, "https://github.com/northwindtraders/vcpkg-registry");
    check_string(serialized_registry, BASELINE, "dacf4de488094a384ca2c202b923ccc097956e0c");
    REQUIRE(registry.packages().size() == 2);
    REQUIRE(registry.packages()[0] == "beicode");
    REQUIRE(registry.packages()[1] == "beison");

    auto raw_obj = parse_json_object(raw_config);
    auto serialized_obj = serialize_configuration(config);
    compare_json_objects(raw_obj, serialized_obj);
}

TEST_CASE ("config without registries", "[ce-metadata]")
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
    REQUIRE(!config.registry_set.registries().size());
    REQUIRE(config.registry_set.is_default_builtin_registry());

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
    const auto& demands = demands_val->object();
    REQUIRE(demands.size() == 1);
    REQUIRE(demands.contains("nested"));
    auto nested_val = demands.get("nested");
    REQUIRE(nested_val->is_object());
    const auto& nested = nested_val->object();
    check_string(nested, CE_MESSAGE, "this is a message too");
    check_string(nested, CE_WARNING, "this is a warning too");
    check_string(nested, CE_ERROR, "this is an error too");
    REQUIRE(nested.contains("$comment"));
    REQUIRE(nested.contains("unexpected"));

    auto raw_obj = parse_json_object(raw_config);
    auto serialized_obj = serialize_configuration(config);
    compare_json_objects(raw_obj, serialized_obj);
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
    }

    SECTION ("invalid json")
    {
        std::string invalid_raw = R"json({
    "message": { "$comment": "this is not a valid message" },
    "warning": 0,
    "error": null
})json";

        auto object = parse_json_object(invalid_raw);

        Json::Reader reader;
        auto deserializer = make_configuration_deserializer("test");
        auto parsed_config_opt = reader.visit(object, *deserializer);
        auto config = parsed_config_opt.get();
        CHECK(config->ce_metadata.size() == 0);
        CHECK_LINES(Strings::join("\n", reader.errors()), R"json(
$.error: mismatched type: expected a string
$.warning: mismatched type: expected a string
$.message: mismatched type: expected a string
)json");
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
    "see-also": {
        "tools/ninja-build/ninja": "1.10.2"
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
        },
        "recurse0": {
            "message": "this is level 0",
            "demands": {
                "recurse1": {
                    "message": "this is level 1",
                    "demands": {
                        "recurse2": {
                            "$comment": "this is the final level",
                            "message": "this is level 2"
                        }
                    }
                }
            }
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
    REQUIRE(config.registry_set.default_registry() != nullptr);

    auto default_registry = std::move(config.registry_set.default_registry()->serialize());
    check_string(default_registry, KIND, "builtin");
    check_string(default_registry, BASELINE, "843e0ba0d8f9c9c572e45564263eedfc7745e74f");

    REQUIRE(config.registry_set.registries().size() == 1);
    const auto& registry = *config.registry_set.registries().begin();
    auto serialized_registry = std::move(registry.implementation().serialize());
    check_string(serialized_registry, KIND, "git");
    check_string(serialized_registry, REPOSITORY, "https://github.com/northwindtraders/vcpkg-registry");
    check_string(serialized_registry, BASELINE, "dacf4de488094a384ca2c202b923ccc097956e0c");
    REQUIRE(registry.packages().size() == 2);
    REQUIRE(registry.packages()[0] == "beicode");
    REQUIRE(registry.packages()[1] == "beison");

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
    const auto& settings = settings_val->object();
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
    const auto& apply = apply_val->object();
    REQUIRE(apply.size() == 2);
    check_string(apply, "key", "value");
    auto apply_complex_key_val = apply.get("complex-key");
    REQUIRE(apply_complex_key_val->is_object());
    const auto& apply_complex_key = apply_complex_key_val->object();
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
    const auto& requires_ = requires_val->object();
    REQUIRE(requires_.size() == 1);
    check_string(requires_, "tools/kitware/cmake", ">=3.21.0");

    /*
    "see-also": {
      "tools/ninja-build/ninja": "1.10.2"
    }
    */
    REQUIRE(ce_metadata.contains(CE_SEE_ALSO));
    auto see_also_val = ce_metadata.get(CE_SEE_ALSO);
    REQUIRE(see_also_val->is_object());
    const auto& see_also = see_also_val->object();
    REQUIRE(see_also.size() == 1);
    check_string(see_also, "tools/ninja-build/ninja", "1.10.2");

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
    const auto& demands = demands_val->object();
    REQUIRE(demands.size() == 3);

    REQUIRE(demands.contains("windows and target:arm"));
    auto demand1_val = demands.get("windows and target:arm");
    REQUIRE(demand1_val->is_object());
    const auto& demand1 = demand1_val->object();
    REQUIRE(demand1.size() == 8);
    check_string(demand1, "$comment", "this is a comment");
    check_string(demand1, "unexpected", "this field does nothing");
    REQUIRE(demand1.get("null")->is_null());
    REQUIRE(demand1.get("number")->number() == 2);
    check_string(demand1, "message", "this is a nested message");
    check_string(demand1, "warning", "this is a nested warning");
    check_string(demand1, "error", "this is a nested error");
    REQUIRE(demand1.contains(CE_REQUIRES));
    auto demand1_requires_val = demand1.get(CE_REQUIRES);
    REQUIRE(demand1_requires_val->is_object());
    const auto& demand1_requires = demand1_requires_val->object();
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
    const auto& demand2 = demand2_val->object();
    REQUIRE(demand2.size() == 5);
    REQUIRE(demand2.contains(CE_ERROR));
    auto demand2_error_val = demand2.get(CE_ERROR);
    REQUIRE(demand2_error_val->is_object());
    const auto& demand2_error = demand2_error_val->object();
    REQUIRE(demand2_error.contains("this would have caused a parser error"));
    REQUIRE(demand2_error.get("this would have caused a parser error")->is_null());
    check_string(demand2, "message", "this would have been ok");
    REQUIRE(demand2.contains(CE_REQUIRES));
    REQUIRE(demand2.get(CE_REQUIRES)->is_null());
    REQUIRE(demand2.contains("what-is-this"));
    REQUIRE(demand2.get("what-is-this")->is_null());
    REQUIRE(demand2.contains("$comment"));
    check_string(demand2, "$comment", "this fields won't be reordered at all");

    /*
      "recurse0": {
        "message": "this is level 0",
        "demands": {
          "recurse1": {
            "message": "this is level 1",
            "demands": {
              "recurse2": {
                "$comment": "this is the final level",
                "message": "this is level 2"
              }
            }
          }
        }
      }
    }
    */
    REQUIRE(demands.contains("recurse0"));
    auto recurse0_val = demands.get("recurse0");
    REQUIRE(recurse0_val->is_object());
    const auto& recurse0 = recurse0_val->object();
    REQUIRE(recurse0.size() == 2);
    check_string(recurse0, CE_MESSAGE, "this is level 0");
    REQUIRE(recurse0.contains(CE_DEMANDS));
    auto recurse0_demands_val = recurse0.get(CE_DEMANDS);
    REQUIRE(recurse0_demands_val->is_object());
    const auto& recurse0_demands = recurse0_demands_val->object();

    REQUIRE(recurse0_demands.contains("recurse1"));
    auto recurse1_val = recurse0_demands.get("recurse1");
    REQUIRE(recurse1_val->is_object());
    const auto& recurse1 = recurse1_val->object();
    REQUIRE(recurse1.size() == 2);
    check_string(recurse1, CE_MESSAGE, "this is level 1");
    REQUIRE(recurse1.contains(CE_DEMANDS));
    auto recurse1_demands_val = recurse1.get(CE_DEMANDS);
    REQUIRE(recurse1_demands_val->is_object());
    const auto& recurse1_demands = recurse1_demands_val->object();

    REQUIRE(recurse1_demands.contains("recurse2"));
    auto recurse2_val = recurse1_demands.get("recurse2");
    REQUIRE(recurse2_val->is_object());
    const auto& recurse2 = recurse2_val->object();
    REQUIRE(recurse2.size() == 2);
    check_string(recurse2, "$comment", "this is the final level");
    check_string(recurse2, CE_MESSAGE, "this is level 2");

    // finally test serialization is OK
    auto raw_obj = parse_json_object(raw_config);
    auto serialized_obj = serialize_configuration(config);
    compare_json_objects(raw_obj, serialized_obj);
}

TEST_CASE ("recursive demands", "[ce-metadata]")
{
    std::string raw = R"json({
    "demands": {
        "nested0": {
            "message": "this is level 0",
            "demands": {
                "nested1": {
                    "message": "this is level 1",
                    "demands": {
                        "nested2": {
                            "message": "this is level 2",
                            "demands": {
                                "nested3": {
                                    "message": "this is level 3, this is the last level"
                                }
                            }
                        }
                    }
                }
            }
        }
    }
})json";

    auto config = parse_test_configuration(raw);
    auto serialized_config = serialize_configuration(config);
    auto raw_obj = parse_json_object(raw);
    REQUIRE(!config.ce_metadata.is_empty());
    REQUIRE(Json::stringify(serialized_config, Json::JsonStyle::with_spaces(4)) ==
            Json::stringify(raw_obj, Json::JsonStyle::with_spaces(4)));
}