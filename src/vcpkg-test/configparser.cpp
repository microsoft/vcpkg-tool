#include <vcpkg-test/util.h>

#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>

using namespace vcpkg;

#if defined(_WIN32)
#define ABSOLUTE_PATH "C:\\foo"
#else
#define ABSOLUTE_PATH "/foo"
#endif

namespace
{
    void validate_readonly_url(const BinaryConfigParserState& state, StringView url)
    {
        auto extended_url = url.to_string() + "/{sha}.zip?sas";
        CHECK(state.url_templates_to_put.empty());
        CHECK(state.url_templates_to_get.size() == 1);
        CHECK(state.url_templates_to_get.front().url_template == extended_url);
    }

    void validate_readonly_sources(const BinaryConfigParserState& state, StringView sources)
    {
        CHECK(state.sources_to_write.empty());
        CHECK(state.sources_to_read.size() == 1);
        CHECK(state.sources_to_read.front() == sources);
    }
}

TEST_CASE ("BinaryConfigParser empty", "[binaryconfigparser]")
{
    auto parsed = parse_binary_provider_configs("", {});
    REQUIRE(parsed.has_value());
}

TEST_CASE ("BinaryConfigParser unacceptable provider", "[binaryconfigparser]")
{
    auto parsed = parse_binary_provider_configs("unacceptable", {});
    REQUIRE(!parsed.has_value());
}

TEST_CASE ("BinaryConfigParser files provider", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("files", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("files,relative-path", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("files,C:foo", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH, {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH ",nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH ",read", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
        REQUIRE(!state.archives_to_read.empty());
        REQUIRE(!Util::Vectors::contains(state.archives_to_write, ABSOLUTE_PATH));
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH ",write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH ",readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
        REQUIRE(!state.archives_to_write.empty());
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH ",readwrite,extra", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("files,,upload", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser nuget source provider", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("nuget", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nuget,relative-path", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        validate_readonly_sources(state, "relative-path");
    }
    {
        auto parsed = parse_binary_provider_configs("nuget,http://example.org/", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        validate_readonly_sources(state, "http://example.org/");
    }
    {
        auto parsed = parse_binary_provider_configs("nuget," ABSOLUTE_PATH, {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        validate_readonly_sources(state, ABSOLUTE_PATH);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
    }
    {
        auto parsed = parse_binary_provider_configs("nuget," ABSOLUTE_PATH ",nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nuget," ABSOLUTE_PATH ",readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        CHECK(state.sources_to_read.size() == 1);
        CHECK(state.sources_to_write.size() == 1);
        CHECK(state.sources_to_read.front() == state.sources_to_write.front());
        CHECK(state.sources_to_read.front() == ABSOLUTE_PATH);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(!state.archives_to_write.empty());
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("nuget," ABSOLUTE_PATH ",readwrite,extra", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nuget,,readwrite", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser nuget timeout", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("nugettimeout,3601", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(state.nugettimeout == std::string{"3601"});
    }
    {
        auto parsed = parse_binary_provider_configs("nugettimeout", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugettimeout,", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugettimeout,nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugettimeout,0", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugettimeout,12x", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugettimeout,-321", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugettimeout,321,123", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser nuget config provider", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("nugetconfig", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugetconfig,relative-path", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugetconfig,http://example.org/", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugetconfig," ABSOLUTE_PATH, {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugetconfig," ABSOLUTE_PATH ",nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugetconfig," ABSOLUTE_PATH ",read", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        CHECK(state.configs_to_write.empty());
        CHECK(state.configs_to_read.size() == 1);
        CHECK(state.configs_to_read.front() == ABSOLUTE_PATH);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("nugetconfig," ABSOLUTE_PATH ",write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        CHECK(state.configs_to_read.empty());
        CHECK(state.configs_to_write.size() == 1);
        CHECK(state.configs_to_write.front() == ABSOLUTE_PATH);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("nugetconfig," ABSOLUTE_PATH ",readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        CHECK(state.configs_to_read.size() == 1);
        CHECK(state.configs_to_write.size() == 1);
        CHECK(state.configs_to_read.front() == state.configs_to_write.front());
        CHECK(state.configs_to_read.front() == ABSOLUTE_PATH);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(!state.archives_to_write.empty());
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("nugetconfig," ABSOLUTE_PATH ",readwrite,extra", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("nugetconfig,,readwrite", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser default provider", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("default", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
    }
    {
        auto parsed = parse_binary_provider_configs("default,nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("default,read", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("default,readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(!state.archives_to_read.empty());
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("default,write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("default,read,extra", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser clear provider", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("clear", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("clear,upload", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser interactive provider", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("interactive", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("interactive,read", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser multiple providers", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("clear;default", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("clear;default,read", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("clear;default,write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("clear;default,readwrite", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("clear;default,readwrite;clear;clear", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("clear;files,relative;default", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs(";;;clear;;;;", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs(";;;,;;;;", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser escaping", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs(";;;;;;;`", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs(";;;;;;;`defaul`t", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH "`", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH "`,", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH "``", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH "```", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH "````", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH ",", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser args", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH, std::vector<std::string>{"clear"});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{"clear"});
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH, std::vector<std::string>{"clear;default"});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"clear"}, {"default"}});
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH, std::vector<std::string>{"clear;default,"});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed =
            parse_binary_provider_configs("files," ABSOLUTE_PATH, std::vector<std::string>{"clear", "clear;default,"});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH, std::vector<std::string>{"clear", "clear"});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{"clear"});
    }
}

TEST_CASE ("BinaryConfigParser azblob provider", "[binaryconfigparser]")
{
    UrlTemplate url_temp;
    {
        auto parsed = parse_binary_provider_configs("x-azblob,https://azure/container,sas", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azblob"}, {"default"}});
        validate_readonly_url(state, "https://azure/container");
        REQUIRE(state.secrets == std::vector<std::string>{"sas"});
    }
    {
        auto parsed = parse_binary_provider_configs("x-azblob,https://azure/container,?sas", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("x-azblob,,sas", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("x-azblob,https://azure/container", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("x-azblob,https://azure/container,sas,invalid", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("x-azblob,https://azure/container,sas,read", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azblob"}, {"default"}});
        validate_readonly_url(state, "https://azure/container");
        REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("x-azblob,https://azure/container,sas,write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azblob"}, {"default"}});
        CHECK(state.url_templates_to_get.empty());
        CHECK(state.url_templates_to_put.empty());
        CHECK(state.azblob_templates_to_put.size() == 1);
        CHECK(state.azblob_templates_to_put.front().url_template == "https://azure/container/{sha}.zip?sas");
        REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("x-azblob,https://azure/container,sas,readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azblob"}, {"default"}});
        CHECK(state.url_templates_to_get.size() == 1);
        CHECK(state.url_templates_to_get.front().url_template == "https://azure/container/{sha}.zip?sas");
        CHECK(state.url_templates_to_put.empty());
        CHECK(state.azblob_templates_to_put.size() == 1);
        CHECK(state.azblob_templates_to_put.front().url_template == "https://azure/container/{sha}.zip?sas");
        REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        REQUIRE(!state.archives_to_read.empty());
        REQUIRE(!state.archives_to_write.empty());
    }
}

TEST_CASE ("BinaryConfigParser azcopy providers", "[binaryconfigparser]")
{
    SECTION ("azcopy no SAS token")
    {
        {
            auto parsed = parse_binary_provider_configs("x-azcopy,https://azure/container", {});
            auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

            REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azcopy"}, {"default"}});
            REQUIRE(state.azcopy_read_templates.size() == 1);
            const auto& azcopy_read = state.azcopy_read_templates.front();
            CHECK(azcopy_read.url == "https://azure/container");
            CHECK(azcopy_read.sas.empty());
            CHECK(azcopy_read.make_object_path("{sha}") == "https://azure/container/{sha}.zip");
            CHECK(azcopy_read.make_container_path() == "https://azure/container");

            CHECK(state.azcopy_write_templates.empty());
            REQUIRE(state.secrets.empty());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy,https://azure/container,read", {});
            auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

            REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azcopy"}, {"default"}});
            REQUIRE(state.azcopy_read_templates.size() == 1);
            const auto& azcopy_read = state.azcopy_read_templates.front();
            CHECK(azcopy_read.url == "https://azure/container");
            CHECK(azcopy_read.sas.empty());
            CHECK(azcopy_read.make_object_path("{sha}") == "https://azure/container/{sha}.zip");
            CHECK(azcopy_read.make_container_path() == "https://azure/container");

            CHECK(state.azcopy_write_templates.empty());
            REQUIRE(state.secrets.empty());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy,https://azure/container,write", {});
            auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

            REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azcopy"}, {"default"}});
            CHECK(state.azcopy_read_templates.empty());
            REQUIRE(state.azcopy_write_templates.size() == 1);
            const auto& azcopy_write = state.azcopy_write_templates.front();
            CHECK(azcopy_write.url == "https://azure/container");
            CHECK(azcopy_write.sas.empty());
            CHECK(azcopy_write.make_object_path("{sha}") == "https://azure/container/{sha}.zip");
            CHECK(azcopy_write.make_container_path() == "https://azure/container");
            REQUIRE(state.secrets.empty());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy,https://azure/container,readwrite", {});
            auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

            REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azcopy"}, {"default"}});
            REQUIRE(state.azcopy_read_templates.size() == 1);
            const auto& azcopy_read = state.azcopy_read_templates.front();
            CHECK(azcopy_read.url == "https://azure/container");
            CHECK(azcopy_read.sas.empty());
            CHECK(azcopy_read.make_object_path("{sha}") == "https://azure/container/{sha}.zip");
            CHECK(azcopy_read.make_container_path() == "https://azure/container");
            REQUIRE(state.azcopy_write_templates.size() == 1);
            const auto& azcopy_write = state.azcopy_write_templates.front();
            CHECK(azcopy_write.url == "https://azure/container");
            CHECK(azcopy_write.sas.empty());
            CHECK(azcopy_write.make_object_path("{sha}") == "https://azure/container/{sha}.zip");
            CHECK(azcopy_write.make_container_path() == "https://azure/container");
            REQUIRE(state.secrets.empty());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy,http://not/container", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy,,readwrite", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy,https://azure/container,", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy,https://azure/container,?sas", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy,https://azure/container,sas,readwrite", {});
            REQUIRE(!parsed.has_value());
        }
    }

    SECTION ("azcopy with SAS token")
    {
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,https://azure/container,sas", {});
            auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
            REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azcopy-sas"}, {"default"}});
            REQUIRE(state.azcopy_read_templates.size() == 1);
            const auto& azcopy_read = state.azcopy_read_templates.front();
            CHECK(azcopy_read.url == "https://azure/container");
            CHECK(azcopy_read.sas == "sas");
            CHECK(azcopy_read.make_object_path("{sha}") == "https://azure/container/{sha}.zip?sas");
            CHECK(azcopy_read.make_container_path() == "https://azure/container?sas");
            CHECK(state.azcopy_write_templates.empty());
            REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,https://azure/container,sas,read", {});
            auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
            REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azcopy-sas"}, {"default"}});
            REQUIRE(state.azcopy_read_templates.size() == 1);
            const auto& azcopy_read = state.azcopy_read_templates.front();
            CHECK(azcopy_read.url == "https://azure/container");
            CHECK(azcopy_read.sas == "sas");
            CHECK(azcopy_read.make_object_path("{sha}") == "https://azure/container/{sha}.zip?sas");
            CHECK(azcopy_read.make_container_path() == "https://azure/container?sas");
            CHECK(state.azcopy_write_templates.empty());
            REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,https://azure/container,sas,write", {});
            auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
            REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azcopy-sas"}, {"default"}});
            CHECK(state.azcopy_read_templates.empty());
            REQUIRE(state.azcopy_write_templates.size() == 1);
            const auto& azcopy_write = state.azcopy_write_templates.front();
            CHECK(azcopy_write.url == "https://azure/container");
            CHECK(azcopy_write.sas == "sas");
            CHECK(azcopy_write.make_object_path("{sha}") == "https://azure/container/{sha}.zip?sas");
            CHECK(azcopy_write.make_container_path() == "https://azure/container?sas");
            REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,https://azure/container,sas,readwrite", {});
            auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
            REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azcopy-sas"}, {"default"}});
            REQUIRE(state.azcopy_read_templates.size() == 1);
            const auto& azcopy_read = state.azcopy_read_templates.front();
            CHECK(azcopy_read.url == "https://azure/container");
            CHECK(azcopy_read.sas == "sas");
            CHECK(azcopy_read.make_object_path("{sha}") == "https://azure/container/{sha}.zip?sas");
            CHECK(azcopy_read.make_container_path() == "https://azure/container?sas");
            REQUIRE(state.azcopy_write_templates.size() == 1);
            const auto& azcopy_write = state.azcopy_write_templates.front();
            CHECK(azcopy_write.url == "https://azure/container");
            CHECK(azcopy_write.sas == "sas");
            CHECK(azcopy_write.make_object_path("{sha}") == "https://azure/container/{sha}.zip?sas");
            CHECK(azcopy_write.make_container_path() == "https://azure/container?sas");
            REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,,sas,readwrite", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,http://not/container", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,https://azure/container", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,https://azure/container,", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,https://azure/container,?sas", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,https://azure/container,,readwrite", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,https://azure/container,sas,invalid", {});
            REQUIRE(!parsed.has_value());
        }
        {
            auto parsed = parse_binary_provider_configs("x-azcopy-sas,https://azure/container,sas,readwrite,extra", {});
            REQUIRE(!parsed.has_value());
        }
    }
}

TEST_CASE ("BinaryConfigParser GCS provider", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("x-gcs,gs://my-bucket/", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.gcs_read_prefixes == std::vector<std::string>{"gs://my-bucket/"});
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
    }
    {
        auto parsed = parse_binary_provider_configs("x-gcs,gs://my-bucket/my-folder", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.gcs_read_prefixes == std::vector<std::string>{"gs://my-bucket/my-folder/"});
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
    }
    {
        auto parsed = parse_binary_provider_configs("x-gcs,", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("x-gcs,gs://my-bucket/my-folder,invalid", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("x-gcs,gs://my-bucket/my-folder,read", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
        REQUIRE(state.gcs_read_prefixes == std::vector<std::string>{"gs://my-bucket/my-folder/"});
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("x-gcs,gs://my-bucket/my-folder,write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
        REQUIRE(state.gcs_write_prefixes == std::vector<std::string>{"gs://my-bucket/my-folder/"});
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = parse_binary_provider_configs("x-gcs,gs://my-bucket/my-folder,readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
        REQUIRE(state.gcs_write_prefixes == std::vector<std::string>{"gs://my-bucket/my-folder/"});
        REQUIRE(state.gcs_read_prefixes == std::vector<std::string>{"gs://my-bucket/my-folder/"});
        REQUIRE(!state.archives_to_write.empty());
        REQUIRE(!state.archives_to_read.empty());
    }
}

TEST_CASE ("BinaryConfigParser HTTP provider", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("http,http://example.org/", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.url_templates_to_get.size() == 1);
        REQUIRE(state.url_templates_to_get[0].url_template == "http://example.org/{sha}.zip");
    }
    {
        auto parsed = parse_binary_provider_configs("http,http://example.org", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.url_templates_to_get.size() == 1);
        REQUIRE(state.url_templates_to_get[0].url_template == "http://example.org/{sha}.zip");
    }
    {
        auto parsed = parse_binary_provider_configs("http,http://example.org/{triplet}/{sha}", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.url_templates_to_get.size() == 1);
        REQUIRE(state.url_templates_to_get[0].url_template == "http://example.org/{triplet}/{sha}");
    }
    {
        auto parsed = parse_binary_provider_configs("http,http://example.org/{triplet}", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser Universal Packages provider", "[binaryconfigparser]")
{
    // Scheme: x-az-universal,<organization>,<project>,<feed>[,<readwrite>]
    {
        auto parsed =
            parse_binary_provider_configs("x-az-universal,test_organization,test_project_name,test_feed,read", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(state.upkg_templates_to_get.size() == 1);
        REQUIRE(state.upkg_templates_to_get[0].feed == "test_feed");
        REQUIRE(state.upkg_templates_to_get[0].organization == "test_organization");
        REQUIRE(state.upkg_templates_to_get[0].project == "test_project_name");
    }
    {
        auto parsed =
            parse_binary_provider_configs("x-az-universal,test_organization,test_project_name,test_feed,readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(state.upkg_templates_to_get.size() == 1);
        REQUIRE(state.upkg_templates_to_put.size() == 1);
        REQUIRE(state.upkg_templates_to_get[0].feed == "test_feed");
        REQUIRE(state.upkg_templates_to_get[0].organization == "test_organization");
        REQUIRE(state.upkg_templates_to_get[0].project == "test_project_name");
        REQUIRE(state.upkg_templates_to_put[0].feed == "test_feed");
        REQUIRE(state.upkg_templates_to_put[0].organization == "test_organization");
        REQUIRE(state.upkg_templates_to_put[0].project == "test_project_name");
    }
    {
        auto parsed = parse_binary_provider_configs(
            "x-az-universal,test_organization,test_project_name,test_feed,extra_argument,readwrite", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = parse_binary_provider_configs("x-az-universal,missing_args,read", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("AssetConfigParser azurl provider", "[assetconfigparser]")
{
    CHECK(parse_download_configuration({}));
    CHECK(!parse_download_configuration("x-azurl"));
    CHECK(!parse_download_configuration("x-azurl,"));
    CHECK(parse_download_configuration("x-azurl,value"));
    CHECK(parse_download_configuration("x-azurl,value,"));
    CHECK(!parse_download_configuration("x-azurl,value,,"));
    CHECK(!parse_download_configuration("x-azurl,value,,invalid"));
    CHECK(parse_download_configuration("x-azurl,value,,read"));
    CHECK(parse_download_configuration("x-azurl,value,,readwrite"));
    CHECK(!parse_download_configuration("x-azurl,value,,readwrite,"));
    CHECK(parse_download_configuration("x-azurl,https://abc/123,?foo"));
    CHECK(parse_download_configuration("x-azurl,https://abc/123,foo"));
    CHECK(parse_download_configuration("x-azurl,ftp://magic,none"));
    CHECK(parse_download_configuration("x-azurl,ftp://magic,none"));

    {
        AssetCachingSettings empty;
        CHECK(empty.m_write_headers.empty());
        CHECK(empty.m_read_headers.empty());
    }
    {
        AssetCachingSettings dm =
            parse_download_configuration("x-azurl,https://abc/123,foo").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>?foo");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
    }
    {
        AssetCachingSettings dm =
            parse_download_configuration("x-azurl,https://abc/123/,foo").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>?foo");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
        CHECK(dm.m_secrets == std::vector<std::string>{"foo"});
    }
    {
        AssetCachingSettings dm =
            parse_download_configuration("x-azurl,https://abc/123,?foo").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>?foo");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
        CHECK(dm.m_secrets == std::vector<std::string>{"?foo"});
    }
    {
        AssetCachingSettings dm =
            parse_download_configuration("x-azurl,https://abc/123").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
    }
    {
        AssetCachingSettings dm =
            parse_download_configuration("x-azurl,https://abc/123,,readwrite").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == "https://abc/123/<SHA>");
        Test::check_ranges(dm.m_write_headers, azure_blob_headers());
    }
    {
        AssetCachingSettings dm =
            parse_download_configuration("x-azurl,https://abc/123,foo,readwrite").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>?foo");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == "https://abc/123/<SHA>?foo");
        Test::check_ranges(dm.m_write_headers, azure_blob_headers());
        CHECK(dm.m_secrets == std::vector<std::string>{"foo"});
    }
    {
        AssetCachingSettings dm =
            parse_download_configuration("x-script,powershell {SHA} {URL}").value_or_exit(VCPKG_LINE_INFO);
        CHECK(!dm.m_read_url_template.has_value());
        CHECK(dm.m_read_headers.empty());
        CHECK(!dm.m_write_url_template.has_value());
        CHECK(dm.m_write_headers.empty());
        CHECK(dm.m_secrets.empty());
        CHECK(dm.m_script.value_or_exit(VCPKG_LINE_INFO) == "powershell {SHA} {URL}");
    }
}

TEST_CASE ("AssetConfigParser clear provider", "[assetconfigparser]")
{
    CHECK(parse_download_configuration("clear"));
    CHECK(!parse_download_configuration("clear,"));
    CHECK(parse_download_configuration("x-azurl,value;clear"));
    auto value_or = [](auto o, auto v) {
        if (o)
            return std::move(*o.get());
        else
            return std::move(v);
    };

    AssetCachingSettings empty;

    CHECK(value_or(parse_download_configuration("x-azurl,https://abc/123,foo;clear"), empty).m_read_url_template ==
          nullopt);
    CHECK(value_or(parse_download_configuration("clear;x-azurl,https://abc/123/,foo"), empty).m_read_url_template ==
          "https://abc/123/<SHA>?foo");
}

TEST_CASE ("AssetConfigParser x-block-origin provider", "[assetconfigparser]")
{
    CHECK(parse_download_configuration("x-block-origin"));
    CHECK(!parse_download_configuration("x-block-origin,"));
    auto value_or = [](auto o, auto v) {
        if (o)
            return std::move(*o.get());
        else
            return std::move(v);
    };

    AssetCachingSettings empty;

    CHECK(!value_or(parse_download_configuration({}), empty).m_block_origin);
    CHECK(value_or(parse_download_configuration("x-block-origin"), empty).m_block_origin);
    CHECK(!value_or(parse_download_configuration("x-block-origin;clear"), empty).m_block_origin);
}
