#pragma once

#include <string>

namespace vcpkg
{
    /// UUID format version 4, variant 1
    /// http://en.wikipedia.org/wiki/Universally_unique_identifier
    /// [0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}
    std::string generate_random_UUID();
}
