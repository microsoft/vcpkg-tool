#pragma once

#include <vcpkg/base/fwd/format.h>

#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>

#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.h>

#include <string>

namespace vcpkg
{
    struct TripletInstance;

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

        explicit operator StringView() const { return canonical_name(); }

        bool operator==(Triplet other) const { return this->m_instance == other.m_instance; }
        bool operator<(Triplet other) const { return canonical_name() < other.canonical_name(); }

    private:
        static const TripletInstance DEFAULT_INSTANCE;

        constexpr Triplet(const TripletInstance* ptr) : m_instance(ptr) { }

        const TripletInstance* m_instance;
    };

    inline bool operator!=(Triplet left, Triplet right) { return !(left == right); }

    Triplet default_triplet(const VcpkgCmdArguments& args);
    Triplet default_host_triplet(const VcpkgCmdArguments& args);
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
