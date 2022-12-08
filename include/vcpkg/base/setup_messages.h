#pragma once

#include <vcpkg/base/fwd/expected.h>

#include <vcpkg/base/json.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>

#include <string>

#include <cmrc/cmrc.hpp>

namespace vcpkg::msg
{

    struct MessageMapAndFile
    {
        Json::Object map;
        cmrc::file map_file;
    };

    void load_from_message_map(const MessageMapAndFile& message_map);

    Optional<std::string> get_locale_path(int LCID);
    Optional<StringLiteral> get_language_tag(int LCID);
    ExpectedS<MessageMapAndFile> get_message_map_from_lcid(int LCID);
    const Optional<cmrc::file> get_file();

}