#include <vcpkg-test/util.h>

#include <vcpkg/base/system.process.h>

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

    auto cgroups = parse_cgroup_file(ok_text, "ok_text", 0);
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

    auto cgroups_short = parse_cgroup_file("2::", "short_text", 0);
    REQUIRE(cgroups_short.size() == 1);
    CHECK(cgroups_short[0].hierarchy_id == 2);
    CHECK(cgroups_short[0].subsystems == "");
    CHECK(cgroups_short[0].control_group == "");

    auto cgroups_incomplete = parse_cgroup_file("0:/", "incomplete_text", 0);
    CHECK(cgroups_incomplete.empty());

    auto cgroups_bad_id = parse_cgroup_file("ab::", "non_numeric_id_text", 0);
    CHECK(cgroups_bad_id.empty());

    auto cgroups_empty = parse_cgroup_file("", "empty", 0);
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

    CHECK(detect_docker_in_cgroup_file(with_docker, "with_docker", 0));
    CHECK(!detect_docker_in_cgroup_file(without_docker, "without_docker", 0));
}

TEST_CASE ("parse proc/pid/stat file", "[cgroup-parser]")
{
    SECTION ("simple case")
    {
        std::string contents =
            R"(4281 (cpptools-srv) S 4099 1676 1676 0 -1 1077936384 51165 303 472 0 81 25 0 0 20 0 10 0 829158 4924583936 39830 18446744073709551615 4194304 14147733 140725993620736 0 0 0 0 16781312 16386 0 0 0 17 1 0 0 5 0 0 16247120 16519160 29999104 140725993622792 140725993622920 140725993622920 140725993627556 0)";

        auto maybe_stat = try_parse_process_stat_file(console_diagnostic_context, {contents, "test"});
        REQUIRE(maybe_stat.has_value());
        auto stat = maybe_stat.value_or_exit(VCPKG_LINE_INFO);
        CHECK(stat.ppid == 4099);
        CHECK(stat.executable_name == "cpptools-srv");
    }

    SECTION ("empty case")
    {
        std::string contents =
            R"(4281 () S 4099 1676 1676 0 -1 1077936384 51165 303 472 0 81 25 0 0 20 0 10 0 829158 4924583936 39830 18446744073709551615 4194304 14147733 140725993620736 0 0 0 0 16781312 16386 0 0 0 17 1 0 0 5 0 0 16247120 16519160 29999104 140725993622792 140725993622920 140725993622920 140725993627556 0)";

        auto maybe_stat = try_parse_process_stat_file(console_diagnostic_context, {contents, "test"});
        REQUIRE(maybe_stat.has_value());
        auto stat = maybe_stat.value_or_exit(VCPKG_LINE_INFO);
        CHECK(stat.ppid == 4099);
        CHECK(stat.executable_name == "");
    }

    SECTION ("comm with parens")
    {
        std::string contents =
            R"(4281 (<(' '<)(> ' ')>) S 4099 1676 1676 0 -1 1077936384 51165 303 472 0 81 25 0 0 20 0 10 0 829158 4924583936 39830 18446744073709551615 4194304 14147733 140725993620736 0 0 0 0 16781312 16386 0 0 0 17 1 0 0 5 0 0 16247120 16519160 29999104 140725993622792 140725993622920 140725993622920 140725993627556 0)";

        auto maybe_stat = try_parse_process_stat_file(console_diagnostic_context, {contents, "test"});
        REQUIRE(maybe_stat.has_value());
        auto stat = maybe_stat.value_or_exit(VCPKG_LINE_INFO);
        CHECK(stat.ppid == 4099);
        CHECK(stat.executable_name == "<(' '<)(> ' ')>");
    }

    SECTION ("comm max length")
    {
        std::string contents =
            R"(4281 (0123456789abcdef) S 4099 1676 1676 0 -1 1077936384 51165 303 472 0 81 25 0 0 20 0 10 0 829158 4924583936 39830 18446744073709551615 4194304 14147733 140725993620736 0 0 0 0 16781312 16386 0 0 0 17 1 0 0 5 0 0 16247120 16519160 29999104 140725993622792 140725993622920 140725993622920 140725993627556 0)";

        auto maybe_stat = try_parse_process_stat_file(console_diagnostic_context, {contents, "test"});
        REQUIRE(maybe_stat.has_value());
        auto stat = maybe_stat.value_or_exit(VCPKG_LINE_INFO);
        CHECK(stat.ppid == 4099);
        CHECK(stat.executable_name == "0123456789abcdef");
    }

    SECTION ("only parens")
    {
        std::string contents =
            R"(4281 (()()()()()()()()) S 4099 1676 1676 0 -1 1077936384 51165 303 472 0 81 25 0 0 20 0 10 0 829158 4924583936 39830 18446744073709551615 4194304 14147733 140725993620736 0 0 0 0 16781312 16386 0 0 0 17 1 0 0 5 0 0 16247120 16519160 29999104 140725993622792 140725993622920 140725993622920 140725993627556 0)";

        auto maybe_stat = try_parse_process_stat_file(console_diagnostic_context, {contents, "test"});
        REQUIRE(maybe_stat.has_value());
        auto stat = maybe_stat.value_or_exit(VCPKG_LINE_INFO);
        CHECK(stat.ppid == 4099);
        CHECK(stat.executable_name == "()()()()()()()()");
    }

    SECTION ("comm too long")
    {
        std::string contents =
            R"(4281 (0123456789abcdefg) S 4099 1676 1676 0 -1 1077936384 51165 303 472 0 81 25 0 0 20 0 10 0 829158 4924583936 39830 18446744073709551615 4194304 14147733 140725993620736 0 0 0 0 16781312 16386 0 0 0 17 1 0 0 5 0 0 16247120 16519160 29999104 140725993622792 140725993622920 140725993622920 140725993627556 0)";

        BufferedDiagnosticContext bdc;
        auto maybe_stat = try_parse_process_stat_file(bdc, {contents, "test"});
        REQUIRE(!maybe_stat.has_value());
        REQUIRE(bdc.to_string() == R"(test:1:7: error: expected ')' here
  on expression: 4281 (0123456789abcdefg) S 4099 1676 1676 0 -1 1077936384 51165 303 472 0 81 25 0 0 20 0 10 0 829158 4924583936 39830 18446744073709551615 4194304 14147733 140725993620736 0 0 0 0 16781312 16386 0 0 0 17 1 0 0 5 0 0 16247120 16519160 29999104 140725993622792 140725993622920 140725993622920 140725993627556 0
                       ^)");
    }
}
