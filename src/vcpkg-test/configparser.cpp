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
    template<typename T>
    constexpr size_t count_read_providers(const ProviderList<T>& providers)
    {
        return std::count_if(
            providers.cbegin(), providers.cend(), [](const CacheProvider<T>& provider) { return provider.is_read(); });
    }

    template<typename T>
    constexpr size_t count_write_providers(const ProviderList<T>& providers)
    {
        return std::count_if(
            providers.cbegin(), providers.cend(), [](const CacheProvider<T>& provider) { return provider.is_write(); });
    }

    template<typename T, typename F>
    constexpr bool has_provider_source(const ProviderList<T>& providers, F&& f)
    {
        return providers.cend() != std::find_if(providers.cbegin(), providers.cend(), f);
    }

    void validate_readonly_url(const BinaryConfigParserState& state, StringView url)
    {
        auto extended_url = url.to_string() + "/{sha}.zip?sas";
        CHECK(count_write_providers(state.url_templates) == 0);
        CHECK(count_read_providers(state.url_templates) == 1);
        CHECK(state.url_templates.front().source.url_template == extended_url);
    }

    void validate_readonly_sources(const BinaryConfigParserState& state, StringView sources)
    {
        CHECK(count_write_providers(state.sources) == 0);
        CHECK(count_read_providers(state.sources) == 1);
        CHECK(state.sources.front().source == sources);
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
        REQUIRE(count_read_providers(state.archives) > 0);
        REQUIRE(!has_provider_source<Path>(state.archives, [&](const CacheProvider<Path>& provider) {
            return provider.is_write() && provider.source == ABSOLUTE_PATH;
        }));
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH ",write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
        REQUIRE(count_write_providers(state.archives) > 0);
    }
    {
        auto parsed = parse_binary_provider_configs("files," ABSOLUTE_PATH ",readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"files"}});
        REQUIRE(count_read_providers(state.archives) > 0);
        REQUIRE(count_write_providers(state.archives) > 0);
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

        CHECK(count_read_providers(state.sources) == 1);
        CHECK(count_write_providers(state.sources) == 1);
        CHECK(state.sources.front().cache_type == CacheType::ReadWrite);
        CHECK(state.sources.front().source == ABSOLUTE_PATH);
        CHECK(state.sources.size() == 1);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(count_read_providers(state.archives) > 0);
        REQUIRE(count_write_providers(state.archives) > 0);
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

        CHECK(count_read_providers(state.configs) == 1);
        CHECK(count_write_providers(state.configs) == 0);
        CHECK(state.configs.front().source == ABSOLUTE_PATH);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(count_read_providers(state.archives) > 0);
    }
    {
        auto parsed = parse_binary_provider_configs("nugetconfig," ABSOLUTE_PATH ",write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        CHECK(count_read_providers(state.configs) == 0);
        CHECK(count_write_providers(state.configs) == 1);
        CHECK(state.configs.front().source == ABSOLUTE_PATH);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(count_write_providers(state.archives) > 0);
    }
    {
        auto parsed = parse_binary_provider_configs("nugetconfig," ABSOLUTE_PATH ",readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        CHECK(count_read_providers(state.configs) == 1);
        CHECK(count_write_providers(state.configs) == 1);
        CHECK(state.configs.front().cache_type == CacheType::ReadWrite);
        CHECK(state.configs.front().source == ABSOLUTE_PATH);
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"nuget"}});
        REQUIRE(count_read_providers(state.archives) > 0);
        REQUIRE(count_write_providers(state.archives) > 0);
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
        REQUIRE(count_read_providers(state.archives) > 0);
    }
    {
        auto parsed = parse_binary_provider_configs("default,readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(count_read_providers(state.archives) > 0);
        REQUIRE(count_write_providers(state.archives) > 0);
    }
    {
        auto parsed = parse_binary_provider_configs("default,write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(count_write_providers(state.archives) > 0);
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
        REQUIRE(count_read_providers(state.archives) > 0);
    }
    {
        auto parsed = parse_binary_provider_configs("x-azblob,https://azure/container,sas,write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azblob"}, {"default"}});
        CHECK(count_read_providers(state.url_templates) == 0);
        CHECK(count_write_providers(state.url_templates) == 1);
        CHECK(state.url_templates.front().source.url_template == "https://azure/container/{sha}.zip?sas");
        REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        REQUIRE(count_write_providers(state.archives) > 0);
    }
    {
        auto parsed = parse_binary_provider_configs("x-azblob,https://azure/container,sas,readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"azblob"}, {"default"}});
        CHECK(count_read_providers(state.url_templates) == 1);
        CHECK(count_write_providers(state.url_templates) == 1);
        CHECK(state.url_templates.front().source.url_template == "https://azure/container/{sha}.zip?sas");
        REQUIRE(state.secrets == std::vector<std::string>{"sas"});
        REQUIRE(count_read_providers(state.archives) > 0);
        REQUIRE(count_write_providers(state.archives) > 0);
    }
}

TEST_CASE ("BinaryConfigParser GCS provider", "[binaryconfigparser]")
{
    {
        auto parsed = parse_binary_provider_configs("x-gcs,gs://my-bucket/", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(count_read_providers(state.gcs_prefixes) == 1);
        REQUIRE(state.gcs_prefixes.front().source == "gs://my-bucket/");
        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
    }
    {
        auto parsed = parse_binary_provider_configs("x-gcs,gs://my-bucket/my-folder", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(count_read_providers(state.gcs_prefixes) == 1);
        REQUIRE(state.gcs_prefixes.front().source == "gs://my-bucket/my-folder/");
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
        REQUIRE(count_read_providers(state.gcs_prefixes) == 1);
        REQUIRE(state.gcs_prefixes.front().source == "gs://my-bucket/my-folder/");
        REQUIRE(count_read_providers(state.archives) > 0);
    }
    {
        auto parsed = parse_binary_provider_configs("x-gcs,gs://my-bucket/my-folder,write", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
        REQUIRE(count_write_providers(state.gcs_prefixes) == 1);
        REQUIRE(state.gcs_prefixes.front().source == "gs://my-bucket/my-folder/");
        REQUIRE(count_write_providers(state.archives) > 0);
    }
    {
        auto parsed = parse_binary_provider_configs("x-gcs,gs://my-bucket/my-folder,readwrite", {});
        auto state = parsed.value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(state.binary_cache_providers == std::set<StringLiteral>{{"default"}, {"gcs"}});
        REQUIRE(count_read_providers(state.gcs_prefixes) == 1);
        REQUIRE(count_write_providers(state.gcs_prefixes) == 1);
        REQUIRE(state.gcs_prefixes.size() == 1);
        REQUIRE(state.gcs_prefixes.front().source == "gs://my-bucket/my-folder/");
        REQUIRE(count_read_providers(state.archives) > 0);
        REQUIRE(count_write_providers(state.archives) > 0);
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
