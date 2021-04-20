#include <catch2/catch.hpp>

#include <vcpkg/binarycaching.h>

using namespace vcpkg;

#if defined(_WIN32)
#define ABSOLUTE_PATH "C:\\foo"
#else
#define ABSOLUTE_PATH "/foo"
#endif

TEST_CASE ("BinaryConfigParser empty", "[binaryconfigparser]")
{
    auto parsed = create_binary_provider_from_configs_pure("", {});
    REQUIRE(parsed.has_value());
}

TEST_CASE ("BinaryConfigParser unacceptable provider", "[binaryconfigparser]")
{
    auto parsed = create_binary_provider_from_configs_pure("unacceptable", {});
    REQUIRE(!parsed.has_value());
}

TEST_CASE ("BinaryConfigParser files provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_provider_from_configs_pure("files", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files,relative-path", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files,C:foo", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH, {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH ",nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH ",read", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH ",write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH ",readwrite", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH ",readwrite,extra", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files,,upload", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser nuget source provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_provider_from_configs_pure("nuget", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nuget,relative-path", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nuget,http://example.org/", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nuget," ABSOLUTE_PATH, {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nuget," ABSOLUTE_PATH ",nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nuget," ABSOLUTE_PATH ",readwrite", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nuget," ABSOLUTE_PATH ",readwrite,extra", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nuget,,readwrite", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser nuget config provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_provider_from_configs_pure("nugetconfig", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nugetconfig,relative-path", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nugetconfig,http://example.org/", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nugetconfig," ABSOLUTE_PATH, {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",read", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",readwrite", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nugetconfig," ABSOLUTE_PATH ",readwrite,extra", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("nugetconfig,,readwrite", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser default provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_provider_from_configs_pure("default", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("default,nonsense", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("default,read", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("default,readwrite", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("default,write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("default,read,extra", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser clear provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_provider_from_configs_pure("clear", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("clear,upload", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser interactive provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_provider_from_configs_pure("interactive", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("interactive,read", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser multiple providers", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_provider_from_configs_pure("clear;default", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("clear;default,read", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("clear;default,write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("clear;default,readwrite", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("clear;default,readwrite;clear;clear", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("clear;files,relative;default", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure(";;;clear;;;;", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure(";;;,;;;;", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser escaping", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_provider_from_configs_pure(";;;;;;;`", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure(";;;;;;;`defaul`t", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH "`", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH "`,", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH "``", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH "```", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH "````", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH ",", {});
        REQUIRE(!parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser args", "[binaryconfigparser]")
{
    {
        auto parsed =
            create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH, std::vector<std::string>{"clear"});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed =
            create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH, std::vector<std::string>{"clear;default"});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH,
                                                               std::vector<std::string>{"clear;default,"});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH,
                                                               std::vector<std::string>{"clear", "clear;default,"});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("files," ABSOLUTE_PATH,
                                                               std::vector<std::string>{"clear", "clear"});
        REQUIRE(parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser azblob provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_provider_from_configs_pure("x-azblob,https://azure/container,sas", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-azblob,https://azure/container,?sas", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-azblob,,sas", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-azblob,https://azure/container", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-azblob,https://azure/container,sas,invalid", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-azblob,https://azure/container,sas,read", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-azblob,https://azure/container,sas,write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-azblob,https://azure/container,sas,readwrite", {});
        REQUIRE(parsed.has_value());
    }
}

TEST_CASE ("BinaryConfigParser GCS provider", "[binaryconfigparser]")
{
    {
        auto parsed = create_binary_provider_from_configs_pure("x-gcs,gs://my-bucket/", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-gcs,gs://my-bucket/my-folder", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-gcs,", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-gcs,gs://my-bucket/my-folder,invalid", {});
        REQUIRE(!parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-gcs,gs://my-bucket/my-folder,read", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-gcs,gs://my-bucket/my-folder,write", {});
        REQUIRE(parsed.has_value());
    }
    {
        auto parsed = create_binary_provider_from_configs_pure("x-gcs,gs://my-bucket/my-folder,readwrite", {});
        REQUIRE(parsed.has_value());
    }
}

TEST_CASE ("AssetConfigParser azurl provider", "[assetconfigparser]")
{
    CHECK(create_download_manager({}));
    CHECK(!create_download_manager("x-azurl"));
    CHECK(!create_download_manager("x-azurl,"));
    CHECK(create_download_manager("x-azurl,value"));
    CHECK(create_download_manager("x-azurl,value,"));
    CHECK(!create_download_manager("x-azurl,value,,"));
    CHECK(!create_download_manager("x-azurl,value,,invalid"));
    CHECK(create_download_manager("x-azurl,value,,read"));
    CHECK(create_download_manager("x-azurl,value,,readwrite"));
    CHECK(!create_download_manager("x-azurl,value,,readwrite,"));
    CHECK(create_download_manager("x-azurl,https://abc/123,?foo"));
    CHECK(create_download_manager("x-azurl,https://abc/123,foo"));
    CHECK(create_download_manager("x-azurl,ftp://magic,none"));
    CHECK(create_download_manager("x-azurl,ftp://magic,none"));
    auto value_or = [](auto o, auto v) {
        if (o)
            return std::move(*o.get());
        else
            return std::move(v);
    };

    Downloads::DownloadManager empty;

    CHECK(value_or(create_download_manager("x-azurl,https://abc/123,foo"), empty).internal_get_read_url_template() ==
          "https://abc/123/<SHA>?foo");
    CHECK(value_or(create_download_manager("x-azurl,https://abc/123/,foo"), empty).internal_get_read_url_template() ==
          "https://abc/123/<SHA>?foo");
    CHECK(value_or(create_download_manager("x-azurl,https://abc/123,?foo"), empty).internal_get_read_url_template() ==
          "https://abc/123/<SHA>?foo");
    CHECK(value_or(create_download_manager("x-azurl,https://abc/123"), empty).internal_get_read_url_template() ==
          "https://abc/123/<SHA>");
}

TEST_CASE ("AssetConfigParser clear provider", "[assetconfigparser]")
{
    CHECK(create_download_manager("clear"));
    CHECK(!create_download_manager("clear,"));
    CHECK(create_download_manager("x-azurl,value;clear"));
    auto value_or = [](auto o, auto v) {
        if (o)
            return std::move(*o.get());
        else
            return std::move(v);
    };

    Downloads::DownloadManager empty;

    CHECK(value_or(create_download_manager("x-azurl,https://abc/123,foo;clear"), empty)
              .internal_get_read_url_template() == nullopt);
    CHECK(value_or(create_download_manager("clear;x-azurl,https://abc/123/,foo"), empty)
              .internal_get_read_url_template() == "https://abc/123/<SHA>?foo");
}

TEST_CASE ("AssetConfigParser x-block-origin provider", "[assetconfigparser]")
{
    CHECK(create_download_manager("x-block-origin"));
    CHECK(!create_download_manager("x-block-origin,"));
    auto value_or = [](auto o, auto v) {
        if (o)
            return std::move(*o.get());
        else
            return std::move(v);
    };

    Downloads::DownloadManager empty;

    CHECK(!value_or(create_download_manager({}), empty).block_origin());
    CHECK(value_or(create_download_manager("x-block-origin"), empty).block_origin());
    CHECK(!value_or(create_download_manager("x-block-origin;clear"), empty).block_origin());
}
