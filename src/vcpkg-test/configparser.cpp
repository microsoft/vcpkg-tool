#include <catch2/catch.hpp>

#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>

#include <vcpkg-test/util.h>

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
    auto parsed = create_binary_providers_from_configs_pure("", {});
    REQUIRE(parsed.has_value());
}

TEST_CASE ("BinaryConfigParser unacceptable provider", "[binaryconfigparser]")
{
    auto parsed = create_binary_providers_from_configs_pure("unacceptable", {});
    REQUIRE(!parsed.has_value());
}

TEST_CASE ("BinaryConfigParser files provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure("files", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files,relative-path", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files,C:foo", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH, {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH ",nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH ",read", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
        REQUIRE(!state.archives_to_read.empty());
        REQUIRE(!Vectors::contains(state.archives_to_write, ABSOLUTE_PATH));
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH ",write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH ",readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
        REQUIRE(!state.archives_to_write.empty());
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH ",readwrite,extra", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files,,upload", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser nuget source provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure("nuget", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nuget,relative-path", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        validate_readonly_sources(state, "relative-path");
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nuget,http://example.org/", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        validate_readonly_sources(state, "http://example.org/");
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nuget," ABSOLUTE_PATH, {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        validate_readonly_sources(state, ABSOLUTE_PATH);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nuget," ABSOLUTE_PATH ",nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nuget," ABSOLUTE_PATH ",readwrite", {});
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
        auto parsed = create_binary_providers_from_configs_pure("nuget," ABSOLUTE_PATH ",readwrite,extra", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nuget,,readwrite", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser nuget timeout", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure("nugettimeout,3601", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(state.nugettimeout == std::string{"3601"});
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugettimeout", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugettimeout,", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugettimeout,nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugettimeout,0", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugettimeout,12x", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugettimeout,-321", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugettimeout,321,123", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser nuget config provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig,relative-path", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig,http://example.org/", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig," ABSOLUTE_PATH, {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",read", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        CHECK(state.configs_to_write.empty());
        CHECK(state.configs_to_read.size() == 1);
        CHECK(state.configs_to_read.front() == ABSOLUTE_PATH);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        CHECK(state.configs_to_read.empty());
        CHECK(state.configs_to_write.size() == 1);
        CHECK(state.configs_to_write.front() == ABSOLUTE_PATH);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",readwrite", {});
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
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",readwrite,extra", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig,,readwrite", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser default provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure("default", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("default,nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("default,read", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("default,readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(!state.archives_to_read.empty());
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("default,write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("default,read,extra", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser clear provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure("clear", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("clear,upload", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser interactive provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure("interactive", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("interactive,read", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser multiple providers", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure("clear;default", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("clear;default,read", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("clear;default,write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("clear;default,readwrite", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("clear;default,readwrite;clear;clear", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("clear;files,relative;default", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure(";;;clear;;;;", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure(";;;,;;;;", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser escaping", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure(";;;;;;;`", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure(";;;;;;;`defaul`t", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH "`", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH "`,", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH "``", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH "```", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH "````", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH ",", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser args", "[binaryconfigparser]")
{
    {
        auto parsed =
            create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH, std::vector<std::string>{"clear"});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{"clear"});
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH,
                                                                std::vector<std::string>{"clear;default"});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"clear"}, {"default"}});
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH,
                                                                std::vector<std::string>{"clear;default,"});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH,
                                                                std::vector<std::string>{"clear", "clear;default,"});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH,
                                                                std::vector<std::string>{"clear", "clear"});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{"clear"});
    }
}

TEST_CASE ("BinaryConfigParser azblob provider", "[binaryconfigparser]")
{
    UrlTemplate url_temp;
    {
        auto parsed = create_binary_providers_from_configs_pure("x-azblob,https://azure/container,sas", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azblob"}, {"default"}});
        validate_readonly_url(state, "https://azure/container");
        REQUIRE(state.secrets == std::vector<std::string>{"sas"});
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-azblob,https://azure/container,?sas", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-azblob,,sas", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-azblob,https://azure/container", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-azblob,https://azure/container,sas,invalid", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-azblob,https://azure/container,sas,read", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azblob"}, {"default"}});
        validate_readonly_url(state, "https://azure/container");
        REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-azblob,https://azure/container,sas,write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azblob"}, {"default"}});
        CHECK(state.url_templates_to_get.empty());
        CHECK(state.url_templates_to_put.size() == 1);
        CHECK(state.url_templates_to_put.front().url_template == "https://azure/container/{sha}.zip?sas");
        REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-azblob,https://azure/container,sas,readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azblob"}, {"default"}});
        CHECK(state.url_templates_to_get.size() == 1);
        CHECK(state.url_templates_to_get.front().url_template == "https://azure/container/{sha}.zip?sas");
        CHECK(state.url_templates_to_put.size() == 1);
        CHECK(state.url_templates_to_put.front().url_template == "https://azure/container/{sha}.zip?sas");
        REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        REQUIRE(!state.archives_to_read.empty());
        REQUIRE(!state.archives_to_write.empty());
    }
}

TEST_CASE ("BinaryConfigParser GCS provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure("x-gcs,gs://my-bucket/", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.gcs_read_prefixes == std::vector<std::string>{"gs://my-bucket/"});
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-gcs,gs://my-bucket/my-folder", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.gcs_read_prefixes == std::vector<std::string>{"gs://my-bucket/my-folder/"});
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-gcs,", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-gcs,gs://my-bucket/my-folder,invalid", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-gcs,gs://my-bucket/my-folder,read", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
        REQUIRE(state.gcs_read_prefixes == std::vector<std::string>{"gs://my-bucket/my-folder/"});
        REQUIRE(!state.archives_to_read.empty());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-gcs,gs://my-bucket/my-folder,write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
        REQUIRE(state.gcs_write_prefixes == std::vector<std::string>{"gs://my-bucket/my-folder/"});
        REQUIRE(!state.archives_to_write.empty());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-gcs,gs://my-bucket/my-folder,readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
        REQUIRE(state.gcs_write_prefixes == std::vector<std::string>{"gs://my-bucket/my-folder/"});
        REQUIRE(state.gcs_read_prefixes == std::vector<std::string>{"gs://my-bucket/my-folder/"});
        REQUIRE(!state.archives_to_write.empty());
        REQUIRE(!state.archives_to_read.empty());
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
        DownloadManagerConfig empty;
        CHECK(empty.m_write_headers.empty());
        CHECK(empty.m_read_headers.empty());
    }
    {
        DownloadManagerConfig dm =
            parse_download_configuration("x-azurl,https://abc/123,foo").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>?foo");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
    }
    {
        DownloadManagerConfig dm =
            parse_download_configuration("x-azurl,https://abc/123/,foo").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>?foo");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
        CHECK(dm.m_secrets == std::vector<std::string>{"foo"});
    }
    {
        DownloadManagerConfig dm =
            parse_download_configuration("x-azurl,https://abc/123,?foo").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>?foo");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
        CHECK(dm.m_secrets == std::vector<std::string>{"?foo"});
    }
    {
        DownloadManagerConfig dm =
            parse_download_configuration("x-azurl,https://abc/123").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
    }
    {
        DownloadManagerConfig dm =
            parse_download_configuration("x-azurl,https://abc/123,,readwrite").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == "https://abc/123/<SHA>");
        Test::check_ranges(dm.m_write_headers, azure_blob_headers());
    }
    {
        DownloadManagerConfig dm =
            parse_download_configuration("x-azurl,https://abc/123,foo,readwrite").value_or_exit(VCPKG_LINE_INFO);
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>?foo");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == "https://abc/123/<SHA>?foo");
        Test::check_ranges(dm.m_write_headers, azure_blob_headers());
        CHECK(dm.m_secrets == std::vector<std::string>{"foo"});
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

    DownloadManagerConfig empty;

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

    DownloadManagerConfig empty;

    CHECK(!value_or(parse_download_configuration({}), empty).m_block_origin);
    CHECK(value_or(parse_download_configuration("x-block-origin"), empty).m_block_origin);
    CHECK(!value_or(parse_download_configuration("x-block-origin;clear"), empty).m_block_origin);
}
