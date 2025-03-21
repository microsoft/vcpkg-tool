#pragma once

#include <vcpkg/base/fwd/fmt.h>
#include <vcpkg/base/fwd/optional.h>
#include <vcpkg/base/fwd/system.h>

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>

#include <vcpkg/base/path.h>
#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg
{
    struct Triplet
    {
    public:
        constexpr Triplet() noexcept : m_instance(&DEFAULT_INSTANCE) { }

        static Triplet from_canonical_name(std::string triplet_as_string);

        const std::string& canonical_name() const;
        const std::string& to_string() const;
        void to_string(std::string& out) const;
        size_t hash_code() const;
        Optional<CPUArchitecture> guess_architecture() const noexcept;

        operator StringView() const { return canonical_name(); }

        bool operator==(Triplet other) const { return this->m_instance == other.m_instance; }
        bool operator<(Triplet other) const { return canonical_name() < other.canonical_name(); }

    private:
        static const TripletInstance DEFAULT_INSTANCE;

        constexpr Triplet(const TripletInstance* ptr) : m_instance(ptr) { }

        const TripletInstance* m_instance;
    };

    inline bool operator!=(Triplet left, Triplet right) { return !(left == right); }

}

VCPKG_FORMAT_AS(vcpkg::Triplet, vcpkg::StringView);

namespace std
{
    template<>
    struct hash<vcpkg::Triplet>
    {
        size_t operator()(vcpkg::Triplet t) const { return t.hash_code(); }
    };
}

namespace vcpkg
{
    Triplet default_triplet(const VcpkgCmdArguments& args, const TripletDatabase& database);
    Triplet default_host_triplet(const VcpkgCmdArguments& args, const TripletDatabase& database);

    struct TripletFile
    {
        std::string name;
        Path location;

        TripletFile(StringView name, StringView location);

        Path get_full_path() const;
    };

    struct TripletDatabase
    {
        Path default_triplet_directory;
        Path community_triplet_directory;
        std::vector<TripletFile> available_triplets;

        Path get_triplet_file_path(Triplet triplet) const;
        bool is_valid_triplet_name(StringView name) const;
        bool is_community_triplet_path(const Path& triplet_dir) const;
        bool is_overlay_triplet_path(const Path& triplet_dir) const;
        bool is_valid_triplet_canonical_name(StringView name) const;
    };
}
