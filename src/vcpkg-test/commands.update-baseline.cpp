#include <vcpkg-test/util.h>

#include <vcpkg/commands.update-baseline.h>

#include <algorithm>
#include <initializer_list>
#include <utility>

using namespace vcpkg;

namespace
{
    ManifestVersionSnapshotEntry port(StringView name,
                                      RequestType request_type,
                                      std::initializer_list<std::pair<Triplet, const char*>> selected)
    {
        std::vector<ManifestVersionSnapshotVersionEntry> versions;
        for (const auto& entry : selected)
        {
            versions.emplace_back(entry.first, Version{StringView{entry.second}, 0});
        }

        std::sort(versions.begin(), versions.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.triplet < rhs.triplet;
        });
        return {name.to_string(), std::move(versions), request_type};
    }
}

TEST_CASE ("update baseline snapshot diff", "[commands.update-baseline]")
{
    const auto host = Triplet::from_canonical_name("x64-host");
    const auto target = Triplet::from_canonical_name("x64-target");
    constexpr auto auto_selected = RequestType::AUTO_SELECTED;
    constexpr auto direct = RequestType::USER_REQUESTED;

    SECTION ("single version on each side stays unqualified")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", auto_selected, {{host, "1.1"}})};
        const ManifestVersionSnapshot new_snapshot{port("shared", auto_selected, {{target, "1.0"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies.empty());
        CHECK(diff.transitive_dependencies == std::vector<std::string>{"shared: 1.1 -> 1.0"});
    }

    SECTION ("matching versions remain unchanged despite triplet membership changing")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", auto_selected, {{host, "1.0"}, {target, "1.0"}})};
        const ManifestVersionSnapshot new_snapshot{port("shared", auto_selected, {{target, "1.0"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies.empty());
        CHECK(diff.transitive_dependencies.empty());
    }

    SECTION ("adding a leg at the existing version makes no version change")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", auto_selected, {{target, "1.0"}})};
        const ManifestVersionSnapshot new_snapshot{port("shared", auto_selected, {{host, "1.0"}, {target, "1.0"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies.empty());
        CHECK(diff.transitive_dependencies.empty());
    }

    SECTION ("a disappearing host is removed, not changed to the target version")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", direct, {{host, "1.1"}, {target, "1.0"}})};
        const ManifestVersionSnapshot new_snapshot{port("shared", auto_selected, {{target, "1.0"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies == std::vector<std::string>{"shared:x64-host: removed: 1.1"});
        CHECK(diff.transitive_dependencies.empty());
    }

    SECTION ("a disappearing target is removed, not changed to the host version")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", auto_selected, {{host, "1.1"}, {target, "1.0"}})};
        const ManifestVersionSnapshot new_snapshot{port("shared", auto_selected, {{host, "1.1"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies.empty());
        CHECK(diff.transitive_dependencies == std::vector<std::string>{"shared:x64-target: removed: 1.0"});
    }

    SECTION ("new host and changed target are reported separately")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", auto_selected, {{target, "1.0"}})};
        const ManifestVersionSnapshot new_snapshot{port("shared", direct, {{host, "2.1"}, {target, "2.0"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies ==
              std::vector<std::string>{"shared:x64-host: new: 2.1", "shared:x64-target: 1.0 -> 2.0"});
        CHECK(diff.transitive_dependencies.empty());
    }

    SECTION ("new target and changed host are reported separately")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", direct, {{host, "1.1"}})};
        const ManifestVersionSnapshot new_snapshot{port("shared", direct, {{host, "2.1"}, {target, "2.0"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies ==
              std::vector<std::string>{"shared:x64-host: 1.1 -> 2.1", "shared:x64-target: new: 2.0"});
        CHECK(diff.transitive_dependencies.empty());
    }

    SECTION ("old disagreement remains qualified even if new versions agree")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", auto_selected, {{host, "1.1"}, {target, "1.0"}})};
        const ManifestVersionSnapshot new_snapshot{port("shared", auto_selected, {{host, "2.0"}, {target, "2.0"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies.empty());
        CHECK(diff.transitive_dependencies ==
              std::vector<std::string>{"shared:x64-host: 1.1 -> 2.0", "shared:x64-target: 1.0 -> 2.0"});
    }

    SECTION ("unchanged host does not hide a target version change")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", auto_selected, {{host, "2.0"}, {target, "1.0"}})};
        const ManifestVersionSnapshot new_snapshot{port("shared", auto_selected, {{host, "2.0"}, {target, "2.1"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies.empty());
        CHECK(diff.transitive_dependencies == std::vector<std::string>{"shared:x64-target: 1.0 -> 2.1"});
    }

    SECTION ("equal old versions split when new versions disagree")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", auto_selected, {{host, "1.0"}, {target, "1.0"}})};
        const ManifestVersionSnapshot new_snapshot{port("shared", auto_selected, {{host, "2.1"}, {target, "2.0"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies.empty());
        CHECK(diff.transitive_dependencies ==
              std::vector<std::string>{"shared:x64-host: 1.0 -> 2.1", "shared:x64-target: 1.0 -> 2.0"});
    }

    SECTION ("both snapshots disagree on host and target versions")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", direct, {{host, "1.1"}, {target, "1.0"}})};
        const ManifestVersionSnapshot new_snapshot{port("shared", direct, {{host, "2.1"}, {target, "2.0"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies ==
              std::vector<std::string>{"shared:x64-host: 1.1 -> 2.1", "shared:x64-target: 1.0 -> 2.0"});
        CHECK(diff.transitive_dependencies.empty());
    }

    SECTION ("whole-port changes preserve classification and collapse equal versions")
    {
        const ManifestVersionSnapshot old_snapshot{port("alpha", direct, {{host, "1.0"}, {target, "1.0"}})};
        const ManifestVersionSnapshot new_snapshot{port("beta", auto_selected, {{host, "2.1"}, {target, "2.0"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, new_snapshot);
        CHECK(diff.direct_dependencies == std::vector<std::string>{"alpha: removed: 1.0"});
        CHECK(diff.transitive_dependencies ==
              std::vector<std::string>{"beta:x64-host: new: 2.1", "beta:x64-target: new: 2.0"});
    }

    SECTION ("whole-port removal qualifies differing versions")
    {
        const ManifestVersionSnapshot old_snapshot{port("shared", auto_selected, {{host, "1.1"}, {target, "1.0"}})};
        const auto diff = calculate_version_snapshot_diff(old_snapshot, {});
        CHECK(diff.direct_dependencies.empty());
        CHECK(diff.transitive_dependencies ==
              std::vector<std::string>{"shared:x64-host: removed: 1.1", "shared:x64-target: removed: 1.0"});
    }
}
