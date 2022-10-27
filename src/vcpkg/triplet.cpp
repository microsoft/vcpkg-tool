#include <vcpkg/base/strings.h>

#include <vcpkg/triplet.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    struct TripletInstance
    {
        TripletInstance(std::string&& s) : value(std::move(s)), hash(std::hash<std::string>()(value)) { }

        const std::string value;
        const size_t hash = 0;

        bool operator==(const TripletInstance& o) const { return o.value == value; }
    };
    const TripletInstance Triplet::DEFAULT_INSTANCE({});
}

namespace std
{
    template<>
    struct hash<vcpkg::TripletInstance>
    {
        size_t operator()(const vcpkg::TripletInstance& t) const { return t.hash; }
    };
}

namespace vcpkg
{
    Triplet Triplet::from_canonical_name(std::string triplet_as_string)
    {
        static std::unordered_set<TripletInstance> g_triplet_instances;
        Strings::ascii_to_lowercase(triplet_as_string.data(), triplet_as_string.data() + triplet_as_string.size());
        const auto p = g_triplet_instances.emplace(std::move(triplet_as_string));
        return &*p.first;
    }

    const std::string& Triplet::canonical_name() const { return this->m_instance->value; }

    const std::string& Triplet::to_string() const { return this->canonical_name(); }
    void Triplet::to_string(std::string& out) const { out.append(this->canonical_name()); }
    size_t Triplet::hash_code() const { return m_instance->hash; }

    Optional<CPUArchitecture> Triplet::guess_architecture() const noexcept
    {
        if (Strings::starts_with(this->canonical_name(), "x86-"))
        {
            return CPUArchitecture::X86;
        }
        if (Strings::starts_with(this->canonical_name(), "x64-"))
        {
            return CPUArchitecture::X64;
        }
        if (Strings::starts_with(this->canonical_name(), "arm-"))
        {
            return CPUArchitecture::ARM;
        }
        if (Strings::starts_with(this->canonical_name(), "arm64-"))
        {
            return CPUArchitecture::ARM64;
        }
        if (Strings::starts_with(this->canonical_name(), "arm64ec-"))
        {
            return CPUArchitecture::ARM64EC;
        }
        if (Strings::starts_with(this->canonical_name(), "s390x-"))
        {
            return CPUArchitecture::S390X;
        }
        if (Strings::starts_with(this->canonical_name(), "ppc64le-"))
        {
            return CPUArchitecture::PPC64LE;
        }

        return nullopt;
    }

    static Triplet system_triplet()
    {
        auto host_proc = get_host_processor();
        auto canonical_name = Strings::format("%s-%s", to_zstring_view(host_proc), get_host_os_name());
        return Triplet::from_canonical_name(std::move(canonical_name));
    }

    Triplet default_triplet(const VcpkgCmdArguments& args)
    {
        if (auto triplet = args.triplet.get())
        {
            return Triplet::from_canonical_name(*triplet);
        }
#if defined(_WIN32)
        return Triplet::from_canonical_name("x86-windows");
#else
        return system_triplet();
#endif
    }

    Triplet default_host_triplet(const VcpkgCmdArguments& args)
    {
        if (auto host_triplet = args.host_triplet.get())
        {
            return Triplet::from_canonical_name(*host_triplet);
        }
        return system_triplet();
    }
}
