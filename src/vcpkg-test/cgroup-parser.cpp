#include <catch2/catch.hpp>

#include <vcpkg/base/stringview.h>

#include <vcpkg/cgroup-parser.h>

using namespace vcpkg;

TEST_CASE ("parse", "[cgroup-parser]")
{
    auto ok_text = R"(
3:cpu:/
2:cpuset:/
1:memory:/
0::/
)";

    auto cgroups = parse_cgroup_file(ok_text, "ok_text");
    REQUIRE(cgroups.size() == 4);
    CHECK(cgroups[0].hierarchy_id == 3);
    CHECK(cgroups[0].subsystems == "cpu");
    CHECK(cgroups[0].control_group == "/");
    CHECK(cgroups[1].hierarchy_id == 2);
    CHECK(cgroups[1].subsystems == "cpuset");
    CHECK(cgroups[1].control_group == "/");
    CHECK(cgroups[2].hierarchy_id == 1);
    CHECK(cgroups[2].subsystems == "memory");
    CHECK(cgroups[2].control_group == "/");
    CHECK(cgroups[3].hierarchy_id == 0);
    CHECK(cgroups[3].subsystems == "");
    CHECK(cgroups[3].control_group == "/");

    auto cgroups_short = parse_cgroup_file("2::", "short_text");
    REQUIRE(cgroups_short.size() == 1);
    CHECK(cgroups_short[0].hierarchy_id == 2);
    CHECK(cgroups_short[0].subsystems == "");
    CHECK(cgroups_short[0].control_group == "");

    auto cgroups_incomplete = parse_cgroup_file("0:/", "incomplete_text");
    CHECK(cgroups_incomplete.empty());

    auto cgroups_bad_id = parse_cgroup_file("ab::", "non_numeric_id_text");
    CHECK(cgroups_bad_id.empty());

    auto cgroups_empty = parse_cgroup_file("", "empty");
    CHECK(cgroups_empty.empty());
}

TEST_CASE ("detect docker", "[cgroup-parser]")
{
    auto with_docker = R"(
2:memory:/docker/66a5f8000f3f2e2a19c3f7d60d870064d26996bdfe77e40df7e3fc955b811d14
1:name=systemd:/docker/66a5f8000f3f2e2a19c3f7d60d870064d26996bdfe77e40df7e3fc955b811d14
0::/docker/66a5f8000f3f2e2a19c3f7d60d870064d26996bdfe77e40df7e3fc955b811d14
)";

    auto without_docker = R"(
3:cpu:/
2:cpuset:/
1:memory:/
0::/
)";

    CHECK(detect_docker_in_cgroup_file(with_docker, "with_docker"));
    CHECK(!detect_docker_in_cgroup_file(without_docker, "without_docker"));
}