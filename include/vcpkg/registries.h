#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/json.h>
#include <vcpkg/base/fwd/optional.h>

#include <vcpkg/fwd/configuration.h>
#include <vcpkg/fwd/registries.h>
#include <vcpkg/fwd/sourceparagraph.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/path.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/versions.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vcpkg
{
    inline constexpr StringLiteral builtin_registry_git_url = "https://github.com/microsoft/vcpkg";

    struct LockFile
    {
        struct EntryData
        {
            std::string reference;
            std::string commit_id;
            bool stale;
        };

        using LockDataType = std::multimap<std::string, EntryData, std::less<>>;
        struct Entry
        {
            LockFile* lockfile;
            LockDataType::iterator data;

            const std::string& reference() const { return data->second.reference; }
            const std::string& commit_id() const { return data->second.commit_id; }
            bool stale() const { return data->second.stale; }
            const std::string& uri() const { return data->first; }

            ExpectedL<Unit> ensure_up_to_date(const VcpkgPaths& paths) const;
        };

        ExpectedL<Entry> get_or_fetch(const VcpkgPaths& paths, StringView repo, StringView reference);

        LockDataType lockdata;
        bool modified = false;
    };

    struct RegistryEntry
    {
        virtual ExpectedL<SourceControlFileAndLocation> try_load_port(const Version& version) const = 0;

        virtual ~RegistryEntry() = default;
    };

    struct RegistryImplementation
    {
        virtual StringLiteral kind() const = 0;

        // If an error occurs, the ExpectedL will be in an error state.
        // Otherwise, if the port is known, returns a pointer to RegistryEntry describing the port.
        // Otherwise, returns a nullptr unique_ptr.
        virtual ExpectedL<std::unique_ptr<RegistryEntry>> get_port_entry(StringView port_name) const = 0;

        // Appends the names of the known ports to the out parameter.
        // May result in duplicated port names; make sure to Util::sort_unique_erase at the end
        virtual ExpectedL<Unit> append_all_port_names(std::vector<std::string>& port_names) const = 0;

        // Appends the names of the ports to the out parameter if this can be known without
        // network access.
        // Returns true iff names were checked without network access.
        virtual ExpectedL<bool> try_append_all_port_names_no_network(std::vector<std::string>& port_names) const = 0;

        // If an error occurs, the ExpectedL will be in an error state.
        // Otherwise, if the port is in the baseline, returns the version that baseline denotes.
        // Otherwise, the Optional is disengaged.
        virtual ExpectedL<Optional<Version>> get_baseline_version(StringView port_name) const = 0;

        virtual ~RegistryImplementation() = default;
    };

    struct Registry
    {
        // requires: static_cast<bool>(implementation)
        Registry(std::vector<std::string>&& patterns, std::unique_ptr<RegistryImplementation>&& implementation);

        Registry(std::vector<std::string>&&, std::nullptr_t) = delete;

        // always ordered lexicographically; note the JSON name is "packages"
        View<std::string> patterns() const { return patterns_; }
        const RegistryImplementation& implementation() const { return *implementation_; }

    private:
        std::vector<std::string> patterns_;
        std::unique_ptr<RegistryImplementation> implementation_;
    };

    // this type implements the registry fall back logic from the registries RFC:
    // A port name maps to one of the non-default registries if that registry declares
    // that it is the registry for that port name, else it maps to the default registry
    // if that registry exists; else, there is no registry for a port.
    // The way one sets this up is via the `"registries"` and `"default_registry"`
    // configuration fields.
    struct RegistrySet
    {
        RegistrySet(std::unique_ptr<RegistryImplementation>&& default_registry, std::vector<Registry>&& registries)
            : default_registry_(std::move(default_registry)), registries_(std::move(registries))
        {
        }

        // finds the correct registry for the port name
        // Returns the null pointer if there is no registry set up for that name
        const RegistryImplementation* registry_for_port(StringView port_name) const;

        // Returns a list of registries that can resolve a given port name
        // the returned list is sorted by priority.
        std::vector<const RegistryImplementation*> registries_for_port(StringView name) const;

        ExpectedL<Optional<Version>> baseline_for_port(StringView port_name) const;

        View<Registry> registries() const { return registries_; }

        const RegistryImplementation* default_registry() const { return default_registry_.get(); }

        bool is_default_builtin_registry() const;

        // returns whether the registry set has any modifications to the default
        // (i.e., whether `default_registry` was set, or `registries` had any entries)
        // for checking against the registry feature flag.
        bool has_modifications() const;

        // Returns a sorted vector of all reachable port names in this set.
        ExpectedL<std::vector<std::string>> get_all_reachable_port_names() const;

        // Returns a sorted vector of all reachable port names we can provably determine without touching the network.
        ExpectedL<std::vector<std::string>> get_all_known_reachable_port_names_no_network() const;

    private:
        std::unique_ptr<RegistryImplementation> default_registry_;
        std::vector<Registry> registries_;
    };

    std::unique_ptr<RegistryImplementation> make_builtin_registry(const VcpkgPaths& paths);
    std::unique_ptr<RegistryImplementation> make_builtin_registry(const VcpkgPaths& paths, std::string baseline);
    std::unique_ptr<RegistryImplementation> make_git_registry(const VcpkgPaths& paths,
                                                              std::string repo,
                                                              std::string reference,
                                                              std::string baseline);
    std::unique_ptr<RegistryImplementation> make_filesystem_registry(const ReadOnlyFilesystem& fs,
                                                                     Path path,
                                                                     std::string baseline);

    struct GitVersionDbEntry
    {
        SchemedVersion version;
        std::string git_tree;
    };

    struct GitVersionsLoadResult
    {
        // If the versions database file does not exist, a disengaged Optional
        // Otherwise, if a file I/O error occurred or the file is malformed, that error
        // Otherwise, the loaded version database records
        ExpectedL<Optional<std::vector<GitVersionDbEntry>>> entries;
        Path versions_file_path;
    };

    GitVersionsLoadResult load_git_versions_file(const ReadOnlyFilesystem& fs,
                                                 const Path& registry_versions,
                                                 StringView port_name);

    struct FullGitVersionsDatabase
    {
        explicit FullGitVersionsDatabase(const ReadOnlyFilesystem& fs,
                                         const Path& registry_versions,
                                         std::map<std::string, GitVersionsLoadResult, std::less<>>&& initial);
        FullGitVersionsDatabase(FullGitVersionsDatabase&&);
        FullGitVersionsDatabase& operator=(FullGitVersionsDatabase&&);

        const GitVersionsLoadResult& lookup(StringView port_name);
        const std::map<std::string, GitVersionsLoadResult, std::less<>>& cache() const;

    private:
        const ReadOnlyFilesystem* m_fs;
        Path m_registry_versions;
        std::map<std::string, GitVersionsLoadResult, std::less<>> m_cache;
    };

    // The outer expected only contains directory enumeration errors; individual parse errors are within
    ExpectedL<FullGitVersionsDatabase> load_all_git_versions_files(const ReadOnlyFilesystem& fs,
                                                                   const Path& registry_versions);

    struct FilesystemVersionDbEntry
    {
        SchemedVersion version;
        Path p;
    };

    ExpectedL<Optional<std::vector<FilesystemVersionDbEntry>>> load_filesystem_versions_file(
        const ReadOnlyFilesystem& fs, const Path& registry_versions, StringView port_name, const Path& registry_root);

    ExpectedL<std::map<std::string, Version, std::less<>>> get_builtin_baseline(const VcpkgPaths& paths);

    // Returns the effective match length of the package pattern `pattern` against `name`.
    // No match is 0, exact match is SIZE_MAX, wildcard match is the length of the pattern.
    // Note that the * is included in the match size to distinguish from 0 == no match.
    size_t package_pattern_match(StringView name, StringView pattern);
}
