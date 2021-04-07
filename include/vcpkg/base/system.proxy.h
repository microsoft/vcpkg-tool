#pragma once

#include <optional>
#include <string>

namespace vcpkg::System
{
    bool get_windows_proxy_enabled();
    std::optional<std::string> get_windows_proxy_server();
}