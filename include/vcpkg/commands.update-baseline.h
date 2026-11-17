#pragma once

#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/triplet.h>
#include <vcpkg/versions.h>

#include <string>
#include <utility>
#include <vector>

namespace vcpkg
{
    struct ManifestVersionSnapshotVersionEntry
    {
        Triplet triplet;
        Version version;

        ManifestVersionSnapshotVersionEntry(Triplet triplet, Version version)
            : triplet(triplet), version(std::move(version))
        {
        }
    };

    struct ManifestVersionSnapshotEntry
    {
        std::string port_name;
        // Entries are sorted by triplet.
        std::vector<ManifestVersionSnapshotVersionEntry> versions;
        RequestType request_type;

        ManifestVersionSnapshotEntry(std::string port_name,
                                     std::vector<ManifestVersionSnapshotVersionEntry> versions,
                                     RequestType request_type)
            : port_name(std::move(port_name)), versions(std::move(versions)), request_type(request_type)
        {
        }
    };

    // These are sorted by port name.
    using ManifestVersionSnapshot = std::vector<ManifestVersionSnapshotEntry>;

    struct PrintableVersionDiff
    {
        std::vector<std::string> direct_dependencies;
        std::vector<std::string> transitive_dependencies;

        void add_version_snapshot_diff_line(RequestType request_type, std::string&& line);
        void print();
    };

    PrintableVersionDiff calculate_version_snapshot_diff(const ManifestVersionSnapshot& previous,
                                                         const ManifestVersionSnapshot& current);

    extern const CommandMetadata CommandUpdateBaselineMetadata;
    void command_update_baseline_and_exit(const VcpkgCmdArguments& args,
                                          const VcpkgPaths& paths,
                                          Triplet default_triplet,
                                          Triplet host_triplet);
}
