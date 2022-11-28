#pragma once

#include <vcpkg/base/fwd/expected.h>

#include <vcpkg/base/json.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg::msg
{
    void load_from_message_map(const Json::Object& message_map);
    Optional<std::string> get_locale_path(int LCID);
    Optional<StringLiteral> get_language_tag(int LCID);
    ExpectedS<Json::Object> get_message_map_from_lcid(int LCID);
}