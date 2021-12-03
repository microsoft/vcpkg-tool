#include <catch2/catch.hpp>

#include <vcpkg/binarycaching.h>

#include <vcpkg-test/util.h>

using namespace vcpkg;

#if defined(_WIN32)
#define ABSOLUTE_PATH "C:\\foo"
#else
#define ABSOLUTE_PATH "/foo"
#endif

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
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH ",nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH ",read", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH ",write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH ",readwrite", {});
        REQUIRE(parsed.has_value());
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
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nuget,http://example.org/", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nuget," ABSOLUTE_PATH, {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nuget," ABSOLUTE_PATH ",nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nuget," ABSOLUTE_PATH ",readwrite", {});
        REQUIRE(parsed.has_value());
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
        REQUIRE(parsed.has_value());
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
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",readwrite", {});
        REQUIRE(parsed.has_value());
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
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("default,nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("default,read", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("default,readwrite", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("default,write", {});
        REQUIRE(parsed.has_value());
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
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH "``", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH "```", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH "````", {});
        REQUIRE(parsed.has_value());
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
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("files," ABSOLUTE_PATH,
                                                                std::vector<std::string>{"clear;default"});
        REQUIRE(parsed.has_value());
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
        REQUIRE(parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser azblob provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure("x-azblob,https://azure/container,sas", {});
        REQUIRE(parsed.has_value());
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
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-azblob,https://azure/container,sas,write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-azblob,https://azure/container,sas,readwrite", {});
        REQUIRE(parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser GCS provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_providers_from_configs_pure("x-gcs,gs://my-bucket/", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-gcs,gs://my-bucket/my-folder", {});
        REQUIRE(parsed.has_value());
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
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-gcs,gs://my-bucket/my-folder,write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_providers_from_configs_pure("x-gcs,gs://my-bucket/my-folder,readwrite", {});
        REQUIRE(parsed.has_value());
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
        DownloadManagerConfig dm = Test::unwrap(parse_download_configuration("x-azurl,https://abc/123,foo"));
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>?foo");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
    }
    {
        DownloadManagerConfig dm = Test::unwrap(parse_download_configuration("x-azurl,https://abc/123/,foo"));
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>?foo");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
        CHECK(dm.m_secrets == std::vector<std::string>{"foo"});
    }
    {
        DownloadManagerConfig dm = Test::unwrap(parse_download_configuration("x-azurl,https://abc/123,?foo"));
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>?foo");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
        CHECK(dm.m_secrets == std::vector<std::string>{"?foo"});
    }
    {
        DownloadManagerConfig dm = Test::unwrap(parse_download_configuration("x-azurl,https://abc/123"));
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == nullopt);
    }
    {
        DownloadManagerConfig dm = Test::unwrap(parse_download_configuration("x-azurl,https://abc/123,,readwrite"));
        CHECK(dm.m_read_url_template == "https://abc/123/<SHA>");
        CHECK(dm.m_read_headers.empty());
        CHECK(dm.m_write_url_template == "https://abc/123/<SHA>");
        Test::check_ranges(dm.m_write_headers, azure_blob_headers());
    }
    {
        DownloadManagerConfig dm = Test::unwrap(parse_download_configuration("x-azurl,https://abc/123,foo,readwrite"));
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
