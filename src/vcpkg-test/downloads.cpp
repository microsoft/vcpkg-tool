#include <vcpkg-test/util.h>

#include <vcpkg/base/downloads.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <random>

using namespace vcpkg;

#define CHECK_EC_ON_FILE(file, ec)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (ec)                                                                                                        \
        {                                                                                                              \
            FAIL((file).native() << ": " << (ec).message());                                                           \
        }                                                                                                              \
    } while (0)

TEST_CASE ("parse_split_url_view", "[downloads]")
{
    {
        auto x = parse_split_url_view("https://github.com/Microsoft/vcpkg");
        if (auto v = x.get())
        {
            REQUIRE(v->scheme == "https");
            REQUIRE(v->authority.value_or("") == "//github.com");
            REQUIRE(v->path_query_fragment == "/Microsoft/vcpkg");
        }
        else
        {
            FAIL();
        }
    }
    {
        REQUIRE(!parse_split_url_view("").has_value());
        REQUIRE(!parse_split_url_view("hello").has_value());
    }
    {
        auto x = parse_split_url_view("file:");
        if (auto y = x.get())
        {
            REQUIRE(y->scheme == "file");
            REQUIRE(!y->authority.has_value());
            REQUIRE(y->path_query_fragment == "");
        }
        else
        {
            FAIL();
        }
    }
    {
        auto x = parse_split_url_view("file:path");
        if (auto y = x.get())
        {
            REQUIRE(y->scheme == "file");
            REQUIRE(!y->authority.has_value());
            REQUIRE(y->path_query_fragment == "path");
        }
        else
        {
            FAIL();
        }
    }
    {
        auto x = parse_split_url_view("file:/path");
        if (auto y = x.get())
        {
            REQUIRE(y->scheme == "file");
            REQUIRE(!y->authority.has_value());
            REQUIRE(y->path_query_fragment == "/path");
        }
        else
        {
            FAIL();
        }
    }
    {
        auto x = parse_split_url_view("file://user:pw@host");
        if (auto y = x.get())
        {
            REQUIRE(y->scheme == "file");
            REQUIRE(y->authority.value_or("") == "//user:pw@host");
            REQUIRE(y->path_query_fragment == "");
        }
        else
        {
            FAIL();
        }
    }
    {
        auto x = parse_split_url_view("ftp://host:port/");
        if (auto y = x.get())
        {
            REQUIRE(y->scheme == "ftp");
            REQUIRE(y->authority.value_or("") == "//host:port");
            REQUIRE(y->path_query_fragment == "/");
        }
        else
        {
            FAIL();
        }
    }
    {
        auto x = parse_split_url_view("file://D:\\work\\testing\\asset-cache/"
                                      "562de7b577c99fe347b00437d14ce375a8e5a60504909cb67d2f73c372d39a2f76d2b42b69e4aeb3"
                                      "1a4879e1bcf6f7c2d41f2ace12180ea83ba7af48879d40ab");
        if (auto y = x.get())
        {
            REQUIRE(y->scheme == "file");
            REQUIRE(y->authority.value_or("") == "//D:\\work\\testing\\asset-cache");
            REQUIRE(y->path_query_fragment == "/562de7b577c99fe347b00437d14ce375a8e5a60504909cb67d2f73c372d39a2f76d2b42"
                                              "b69e4aeb31a4879e1bcf6f7c2d41f2ace12180ea83ba7af48879d40ab");
        }
        else
        {
            FAIL();
        }
    }
}

TEST_CASE ("parse_curl_status_line", "[downloads]")
{
    std::vector<int> http_codes;
    StringLiteral malformed_examples[] = {
        "asdfasdf",                                       // wrong prefix
        "curl: unknown --write-out variable: 'exitcode'", // wrong prefixes, and also what old curl does
        "curl: unknown --write-out variable: 'errormsg'",
        "prefix",      // missing spaces
        "prefix42",    // missing spaces
        "prefix42 2",  // missing space
        "prefix42 2a", // non numeric exitcode
    };

    FullyBufferedDiagnosticContext bdc;
    for (auto&& malformed : malformed_examples)
    {
        REQUIRE(!parse_curl_status_line(bdc, http_codes, "prefix", malformed));
        REQUIRE(http_codes.empty());
        REQUIRE(bdc.empty());
    }

    // old curl output
    REQUIRE(!parse_curl_status_line(bdc, http_codes, "prefix", "prefix200  "));
    REQUIRE(http_codes == std::vector<int>{200});
    REQUIRE(bdc.empty());
    http_codes.clear();

    REQUIRE(!parse_curl_status_line(bdc, http_codes, "prefix", "prefix404  "));
    REQUIRE(http_codes == std::vector<int>{404});
    REQUIRE(bdc.empty());
    http_codes.clear();

    REQUIRE(!parse_curl_status_line(bdc, http_codes, "prefix", "prefix0  ")); // a failure, but we don't know that yet
    REQUIRE(http_codes == std::vector<int>{0});
    REQUIRE(bdc.empty());
    http_codes.clear();

    // current curl output
    REQUIRE(parse_curl_status_line(bdc, http_codes, "prefix", "prefix200 0 "));
    REQUIRE(http_codes == std::vector<int>{200});
    REQUIRE(bdc.empty());
    http_codes.clear();

    REQUIRE(parse_curl_status_line(
        bdc,
        http_codes,
        "prefix",
        "prefix0 60 schannel: SNI or certificate check failed: SEC_E_WRONG_PRINCIPAL (0x80090322) "
        "- The target principal name is incorrect."));
    REQUIRE(http_codes == std::vector<int>{0});
    REQUIRE(bdc.to_string() ==
            "error: curl operation failed with error code 60. schannel: SNI or certificate check failed: "
            "SEC_E_WRONG_PRINCIPAL (0x80090322) - The target principal name is incorrect.");
}

TEST_CASE ("download_files", "[downloads]")
{
    auto const dst = Test::base_temporary_directory() / "download_files";
    auto const url = [&](std::string l) -> auto { return std::pair(l, dst); };

    FullyBufferedDiagnosticContext bdc;
    std::vector<std::string> headers;
    std::vector<std::string> secrets;
    auto results = download_files_no_cache(
        bdc,
        std::vector{url("unknown://localhost:9/secret"), url("http://localhost:9/not-exists/secret")},
        headers,
        secrets);
    REQUIRE(results == std::vector<int>{0, 0});
    auto all_errors = bdc.to_string();
    if (all_errors == "error: curl operation failed with error code 7.")
    {
        // old curl, this is OK!
    }
    else
    {
        // new curl
        REQUIRE_THAT(
            all_errors,
            Catch::Matches("error: curl operation failed with error code 1\\. Protocol \"unknown\" not supported( or "
                           "disabled in libcurl)?\n"
                           "error: curl operation failed with error code 7\\. Failed to connect to localhost port 9 "
                           "after [0-9]+ ms: ((Could not|Couldn't) connect to server|Connection refused)",
                           Catch::CaseSensitive::Yes));
    }
}

TEST_CASE ("try_parse_curl_max5_size", "[downloads]")
{
    REQUIRE(!try_parse_curl_max5_size("").has_value());
    REQUIRE(!try_parse_curl_max5_size("hi").has_value());
    REQUIRE(try_parse_curl_max5_size("0").value_or_exit(VCPKG_LINE_INFO) == 0ull);
    REQUIRE(try_parse_curl_max5_size("1").value_or_exit(VCPKG_LINE_INFO) == 1ull);
    REQUIRE(try_parse_curl_max5_size("10").value_or_exit(VCPKG_LINE_INFO) == 10ull);
    REQUIRE(!try_parse_curl_max5_size("10 ").has_value()); // no unknown suffixes
    REQUIRE(try_parse_curl_max5_size("100").value_or_exit(VCPKG_LINE_INFO) == 100ull);
    REQUIRE(try_parse_curl_max5_size("100").value_or_exit(VCPKG_LINE_INFO) == 100ull);
    REQUIRE(try_parse_curl_max5_size("1000").value_or_exit(VCPKG_LINE_INFO) == 1000ull);
    REQUIRE(!try_parse_curl_max5_size("1000.").has_value()); // dot needs 1 or 2 digits
    REQUIRE(!try_parse_curl_max5_size("1000.k").has_value());
    // fails in parsing the number:
    REQUIRE(!try_parse_curl_max5_size("18446744073709551616").has_value());

    // suffixes are 1024'd
    REQUIRE(try_parse_curl_max5_size("1k").value_or_exit(VCPKG_LINE_INFO) == (1ull << 10));
    REQUIRE(try_parse_curl_max5_size("1M").value_or_exit(VCPKG_LINE_INFO) == (1ull << 20));
    REQUIRE(try_parse_curl_max5_size("1G").value_or_exit(VCPKG_LINE_INFO) == (1ull << 30));
    REQUIRE(try_parse_curl_max5_size("1T").value_or_exit(VCPKG_LINE_INFO) == (1ull << 40));
    REQUIRE(try_parse_curl_max5_size("1P").value_or_exit(VCPKG_LINE_INFO) == (1ull << 50));
    REQUIRE(!try_parse_curl_max5_size("1a").has_value());

    // 1.3*1024 == 1'331.2
    REQUIRE(try_parse_curl_max5_size("1.3k").value_or_exit(VCPKG_LINE_INFO) == 1331ull);
    // 1.33*1024 == 1'361.92
    REQUIRE(try_parse_curl_max5_size("1.33k").value_or_exit(VCPKG_LINE_INFO) == 1361ull);

    // 1.3*1024*1024 == 1'363'148.8
    REQUIRE(try_parse_curl_max5_size("1.3M").value_or_exit(VCPKG_LINE_INFO) == 1363148ull);
    // 1.33*1024*1024 == 1'394'606.08
    REQUIRE(try_parse_curl_max5_size("1.33M").value_or_exit(VCPKG_LINE_INFO) == 1394606ull);

    // 1.3*1024*1024*1024 == 1'395'864'371.2
    REQUIRE(try_parse_curl_max5_size("1.3G").value_or_exit(VCPKG_LINE_INFO) == 1395864371ull);
    // 1.33*1024*1024*1024 == 1'428'076'625.92
    REQUIRE(try_parse_curl_max5_size("1.33G").value_or_exit(VCPKG_LINE_INFO) == 1428076625ull);

    // 1.3*1024*1024*1024*1024 == 1'429'365'116'108.8
    REQUIRE(try_parse_curl_max5_size("1.3T").value_or_exit(VCPKG_LINE_INFO) == 1429365116108ull);
    // 1.33*1024*1024*1024*1024 == 1'462'350'464'942.08
    REQUIRE(try_parse_curl_max5_size("1.33T").value_or_exit(VCPKG_LINE_INFO) == 1462350464942ull);

    // 1.3*1024*1024*1024*1024*1024 == 1'463'669'878'895'411.2
    REQUIRE(try_parse_curl_max5_size("1.3P").value_or_exit(VCPKG_LINE_INFO) == 1463669878895411ull);
    // 1.33*1024*1024*1024*1024*1024 == 1'497'446'876'100'689.92
    REQUIRE(try_parse_curl_max5_size("1.33P").value_or_exit(VCPKG_LINE_INFO) == 1497446876100689ull);
}

TEST_CASE ("try_parse_curl_progress_data", "[downloads]")
{
    //  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
    //                                 Dload  Upload   Total   Spent    Left  Speed
    //
    //  0     0    0     0    0     0      0      0 --:--:-- --:--:-- --:--:--     0
    // 100   242  100   242    0     0    298      0 --:--:-- --:--:-- --:--:--   298
    // 100   242  100   242    0     0    297      0 --:--:-- --:--:-- --:--:--   297
    //
    //  0     0    0     0    0     0      0      0 --:--:--  0:00:01 --:--:--     0
    //  0  190M    0  511k    0     0   199k      0  0:16:19  0:00:02  0:16:17  548k
    //  0  190M    0 1423k    0     0   410k      0  0:07:55  0:00:03  0:07:52  776k
    //  1  190M    1 2159k    0     0   468k      0  0:06:56  0:00:04  0:06:52  726k
    //  1  190M    1 2767k    0     0   499k      0  0:06:30  0:00:05  0:06:25  709k
    //  1  190M    1 3327k    0     0   507k      0  0:06:24  0:00:06  0:06:18  676k
    //  2  190M    2 3935k    0     0   519k      0  0:06:15  0:00:07  0:06:08  683k

    REQUIRE(
        !try_parse_curl_progress_data("  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current")
             .has_value());

    REQUIRE(
        !try_parse_curl_progress_data("                                Dload  Upload   Total   Spent    Left  Speed")
             .has_value());

    {
        const auto out = try_parse_curl_progress_data(
                             "  0     0    0     0    0     0      0      0 --:--:-- --:--:-- --:--:--     0")
                             .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(out.total_percent == 0);
        REQUIRE(out.total_size == 0);
        REQUIRE(out.received_percent == 0);
        REQUIRE(out.received_size == 0);
        REQUIRE(out.transfer_percent == 0);
        REQUIRE(out.transfer_size == 0);
        REQUIRE(out.average_upload_speed == 0);
        REQUIRE(out.average_download_speed == 0);
        REQUIRE(out.current_speed == 0);
    }

    {
        const auto out = try_parse_curl_progress_data(
                             "  2  190M    2 3935k    0     0   519k      0  0:06:15  0:00:07  0:06:08  683k")
                             .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(out.total_percent == 2);
        REQUIRE(out.total_size == 190 * 1024 * 1024);
        REQUIRE(out.received_percent == 2);
        REQUIRE(out.received_size == 3935 * 1024);
        REQUIRE(out.transfer_percent == 0);
        REQUIRE(out.transfer_size == 0);
        REQUIRE(out.average_upload_speed == 0);
        REQUIRE(out.average_download_speed == 519 * 1024);
        REQUIRE(out.current_speed == 683 * 1024);
    }
}

TEST_CASE ("url_encode_spaces", "[downloads]")
{
    REQUIRE(url_encode_spaces("https://example.com?query=value&query2=value2") ==
            "https://example.com?query=value&query2=value2");
    REQUIRE(url_encode_spaces("https://example.com/a/b?query=value&query2=value2") ==
            "https://example.com/a/b?query=value&query2=value2");
    REQUIRE(url_encode_spaces("https://example.com/a%20space/b?query=value&query2=value2") ==
            "https://example.com/a%20space/b?query=value&query2=value2");
    REQUIRE(url_encode_spaces("https://example.com/a space/b?query=value&query2=value2") ==
            "https://example.com/a%20space/b?query=value&query2=value2");
    REQUIRE(url_encode_spaces("https://example.com/a  space/b?query=value&query2=value2") ==
            "https://example.com/a%20%20space/b?query=value&query2=value2");
}

/*
 * To run this test:
 * - Set environment variables VCPKG_TEST_AZBLOB_URL and VCPKG_TEST_AZBLOB_SAS.
 *   (Use Azurite for creating a local test environment, and
 *   Azure Storage Explorer for getting a suitable Shared Access Signature.)
 * - Run 'vcpkg-test azblob [-s]'.
 */
TEST_CASE ("azblob", "[.][azblob]")
{
    auto maybe_url = vcpkg::get_environment_variable("VCPKG_TEST_AZBLOB_URL");
    REQUIRE(maybe_url.has_value());
    std::string url = maybe_url.value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(!url.empty());

    if (url.back() != '/') url += '/';

    auto maybe_sas = vcpkg::get_environment_variable("VCPKG_TEST_AZBLOB_SAS");
    REQUIRE(maybe_sas.has_value());
    std::string query_string = maybe_sas.value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(!query_string.empty());

    if (query_string.front() != '?') query_string += '?' + query_string;

    auto& fs = real_filesystem;
    auto temp_dir = Test::base_temporary_directory() / "azblob";
    fs.remove_all(temp_dir, VCPKG_LINE_INFO);

    std::error_code ec;
    fs.create_directories(temp_dir, ec);
    CHECK_EC_ON_FILE(temp_dir, ec);

    const char* data = "(blob content)";
    auto data_filepath = temp_dir / "data";
    CAPTURE(data_filepath);
    fs.write_contents(data_filepath, data, ec);
    CHECK_EC_ON_FILE(data_filepath, ec);

    auto rnd = Strings::b32_encode(std::mt19937_64()());
    std::vector<std::pair<std::string, Path>> url_pairs;
    {
        auto plain_put_filename = "plain_put_" + rnd;
        auto plain_put_url = url + plain_put_filename + query_string;
        url_pairs.emplace_back(plain_put_url, temp_dir / plain_put_filename);

        FullyBufferedDiagnosticContext diagnostics{};
        auto plain_put_success = store_to_asset_cache(
            diagnostics, plain_put_url, SanitizedUrl{url, {}}, "PUT", azure_blob_headers(), data_filepath);
        INFO(diagnostics.to_string());
        CHECK(plain_put_success);
    }

    {
        auto azcopy_put_filename = "azcopy_put_" + rnd;
        auto azcopy_put_url = url + azcopy_put_filename + query_string;
        url_pairs.emplace_back(azcopy_put_url, temp_dir / azcopy_put_filename);

        FullyBufferedDiagnosticContext diagnostics{};
        auto azcopy_put_success =
            azcopy_to_asset_cache(diagnostics, azcopy_put_url, SanitizedUrl{url, {}}, data_filepath);
        INFO(diagnostics.to_string());
        CHECK(azcopy_put_success);
    }

    {
        FullyBufferedDiagnosticContext diagnostics{};
        auto results = download_files_no_cache(diagnostics, url_pairs, azure_blob_headers(), {});
        INFO(diagnostics.to_string());
        CHECK(results == std::vector<int>{200, 200});
    }

    for (auto& download : url_pairs)
    {
        auto download_filepath = download.second;
        CAPTURE(download_filepath);
        CHECK(fs.read_contents(download_filepath, VCPKG_LINE_INFO) == data);
    }

    fs.remove_all(temp_dir, VCPKG_LINE_INFO);
}
