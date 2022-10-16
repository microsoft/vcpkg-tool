#pragma once

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/system.h>

#include <vcpkg/dependencies.h>

#include <vector>

namespace vcpkg::Export::Prefab
{
    constexpr int kFragmentSize = 3;

    struct Options
    {
        Optional<std::string> maybe_group_id;
        Optional<std::string> maybe_artifact_id;
        Optional<std::string> maybe_version;
        Optional<std::string> maybe_min_sdk;
        Optional<std::string> maybe_target_sdk;
        bool enable_maven = false;
        bool enable_debug = false;
    };
    struct NdkVersion
    {
        NdkVersion(int _major, int _minor, int _patch) : m_major{_major}, m_minor{_minor}, m_patch{_patch} { }
        int major() const { return this->m_major; }
        int minor() const { return this->m_minor; }
        int patch() const { return this->m_patch; }
        std::string to_string();
        void to_string(std::string& out);

        friend bool operator==(const NdkVersion& lhs, const NdkVersion& rhs)
        {
            return lhs.m_major == rhs.m_major && lhs.m_minor == rhs.m_minor && lhs.m_patch == rhs.m_patch;
        }
        friend bool operator!=(const NdkVersion& lhs, const NdkVersion& rhs) { return !(lhs == rhs); }

    private:
        int m_major;
        int m_minor;
        int m_patch;
    };

    struct ABIMetadata
    {
        std::string abi;
        int api;
        int ndk;
        std::string stl;
        std::string to_string();
    };

    struct PlatformModuleMetadata
    {
        std::vector<std::string> export_libraries;
        std::string library_name;
        std::string to_json();
    };

    struct ModuleMetadata
    {
        std::vector<std::string> export_libraries;
        std::string library_name;
        PlatformModuleMetadata android;
        std::string to_json();
    };

    struct PackageMetadata
    {
        std::string name;
        int schema;
        std::vector<std::string> dependencies;
        std::string version;
        std::string to_json();
    };

    void do_export(const std::vector<ExportPlanAction>& export_plan,
                   const VcpkgPaths& paths,
                   const Options& prefab_options,
                   const Triplet& triplet);
    Optional<StringView> find_ndk_version(StringView content);
    Optional<NdkVersion> to_version(StringView version);
}
