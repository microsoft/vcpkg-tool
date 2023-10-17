#include <catch2/catch.hpp>

#include <vcpkg/base/downloads.h>
#include <vcpkg/base/expected.h>

using namespace vcpkg;

TEST_CASE ("split_uri_view", "[downloads]")
{
    {
        auto x = split_uri_view("https://github.com/Microsoft/vcpkg");
        REQUIRE(x.has_value());
        REQUIRE(x.get()->scheme == "https");
        REQUIRE(x.get()->authority.value_or("") == "//github.com");
        REQUIRE(x.get()->path_query_fragment == "/Microsoft/vcpkg");
    }
    {
        auto x = split_uri_view("");
        REQUIRE(!x.has_value());
    }
    {
        auto x = split_uri_view("hello");
        REQUIRE(!x.has_value());
    }
    {
        auto x = split_uri_view("file:");
        REQUIRE(x.has_value());
        REQUIRE(x.get()->scheme == "file");
        REQUIRE(!x.get()->authority.has_value());
        REQUIRE(x.get()->path_query_fragment == "");
    }
    {
        auto x = split_uri_view("file:path");
        REQUIRE(x.has_value());
        REQUIRE(x.get()->scheme == "file");
        REQUIRE(!x.get()->authority.has_value());
        REQUIRE(x.get()->path_query_fragment == "path");
    }
    {
        auto x = split_uri_view("file:/path");
        REQUIRE(x.has_value());
        REQUIRE(x.get()->scheme == "file");
        REQUIRE(!x.get()->authority.has_value());
        REQUIRE(x.get()->path_query_fragment == "/path");
    }
    {
        auto x = split_uri_view("file://user:pw@host");
        REQUIRE(x.has_value());
        REQUIRE(x.get()->scheme == "file");
        REQUIRE(x.get()->authority.value_or({}) == "//user:pw@host");
        REQUIRE(x.get()->path_query_fragment == "");
    }
    {
        auto x = split_uri_view("ftp://host:port/");
        REQUIRE(x.has_value());
        REQUIRE(x.get()->scheme == "ftp");
        REQUIRE(x.get()->authority.value_or({}) == "//host:port");
        REQUIRE(x.get()->path_query_fragment == "/");
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
        REQUIRE(out.recieved_percent == 0);
        REQUIRE(out.recieved_size == 0);
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
        REQUIRE(out.recieved_percent == 2);
        REQUIRE(out.recieved_size == 3935 * 1024);
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
