#pragma once

#include <vcpkg/base/fwd/span.h>
#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/optional.h>

#include <string>

namespace vcpkg
{
    std::string get_user_mac_hash();

    // exposing helper functions for testing
    bool validate_mac_address_format(StringView mac);
    bool is_valid_mac_for_telemetry(StringView mac);
    std::string mac_bytes_to_string(const Span<char>& bytes);
    Optional<std::string> extract_mac_from_getmac_output_line(DiagnosticContext& context, StringView line);
}
