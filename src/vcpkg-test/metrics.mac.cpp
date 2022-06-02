#include <catch2/catch.hpp>

#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.mac.h>

using namespace vcpkg;

static constexpr StringLiteral GOOD_ZERO_MAC = "00:00:00:00:00:00";
static constexpr StringLiteral NON_ZERO_MAC = "90:df:f7:db:45:cc";
static constexpr StringLiteral ALL_FS_MAC = "ff:ff:ff:ff:ff:ff";
static constexpr StringLiteral IBRIDGE_MAC = "ac:de:48:00:11:22";
static constexpr StringLiteral BAD_ZERO_MAC = "00-00-00-00-00-00";
static constexpr StringLiteral NOT_A_MAC = "00:00:no:jk:00:00";
static constexpr StringLiteral EMPTY_MAC = "";

TEST_CASE ("validate MAC address format", "[metrics.map]")
{
    CHECK(validate_mac_address_format(GOOD_ZERO_MAC));
    CHECK(validate_mac_address_format(NON_ZERO_MAC));
    CHECK(validate_mac_address_format(ALL_FS_MAC));
    CHECK(validate_mac_address_format(IBRIDGE_MAC));

    CHECK(!validate_mac_address_format(BAD_ZERO_MAC));
    CHECK(!validate_mac_address_format(NOT_A_MAC));
    CHECK(!validate_mac_address_format(EMPTY_MAC));
}

TEST_CASE ("validate MAC address for telemetry", "[metrics.map]")
{
    CHECK(is_valid_mac_for_telemetry(NON_ZERO_MAC));

    CHECK(!is_valid_mac_for_telemetry(GOOD_ZERO_MAC));
    CHECK(!is_valid_mac_for_telemetry(ALL_FS_MAC));
    CHECK(!is_valid_mac_for_telemetry(IBRIDGE_MAC));
    CHECK(!is_valid_mac_for_telemetry(BAD_ZERO_MAC));
    CHECK(!is_valid_mac_for_telemetry(NOT_A_MAC));
    CHECK(!is_valid_mac_for_telemetry(EMPTY_MAC));
}

TEST_CASE ("MAC bytes to string", "[metrics.map]")
{
    static unsigned char bytes[]{0x00, 0x11, 0x22, 0xdd, 0xee, 0xff};

    auto mac_str = mac_bytes_to_string(Span<unsigned char>(bytes, 6));
    CHECK(mac_str == "00:11:22:dd:ee:ff");
}

TEST_CASE ("test getmac ouptut parse", "[metrics.map]")
{
    std::string mac_str;

    static constexpr StringLiteral good_line =
        R"csv("Wi-Fi","Wi-Fi 6, maybe","00-11-22-DD-EE-FF","\Device\Tcip_{GUID}")csv";
    CHECK(extract_mac_from_getmac_output_line(good_line, mac_str));
    CHECK(mac_str == "00:11:22:dd:ee:ff");

    static constexpr StringLiteral bad_line = "00-11-22-DD-EE-FF      \\Device\\Tcip_{GUID}";
    CHECK(!extract_mac_from_getmac_output_line(bad_line, mac_str));
    CHECK(mac_str.empty());
}
