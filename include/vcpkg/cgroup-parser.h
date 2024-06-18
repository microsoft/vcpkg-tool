#pragma once

#include <vcpkg/base/fwd/stringview.h>

#include <string>
#include <vector>

namespace vcpkg
{
    struct ControlGroup
    {
        long hierarchy_id;
        std::string subsystems;
        std::string control_group;

        ControlGroup(long id, StringView s, StringView c);
    };

    std::vector<ControlGroup> parse_cgroup_file(StringView text, StringView origin, int init_row);

    bool detect_docker_in_cgroup_file(StringView text, StringView origin, int init_row);
}
