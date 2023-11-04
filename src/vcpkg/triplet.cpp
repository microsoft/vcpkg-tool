#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <vcpkg/input.h>
#include <vcpkg/packagespec.h>
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
        Strings::inplace_ascii_to_lowercase(triplet_as_string);
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
        if (Strings::starts_with(this->canonical_name(), "riscv32-"))
        {
            return CPUArchitecture::RISCV32;
        }
        if (Strings::starts_with(this->canonical_name(), "riscv64-"))
        {
            return CPUArchitecture::RISCV64;
        }
        if (Strings::starts_with(this->canonical_name(), "loongarch32-"))
        {
            return CPUArchitecture::LOONGARCH32;
        }
        if (Strings::starts_with(this->canonical_name(), "loongarch64-"))
        {
            return CPUArchitecture::LOONGARCH64;
        }
        if (Strings::starts_with(this->canonical_name(), "mips64-"))
        {
            return CPUArchitecture::MIPS64;
        }

        return nullopt;
    }

    static std::string system_triplet_canonical_name()
    {
        auto host_proc = get_host_processor();
        return fmt::format("{}-{}", to_zstring_view(host_proc), get_host_os_name());
    }

    Triplet default_triplet(const VcpkgCmdArguments& args, const TripletDatabase& database)
    {
        if (auto triplet_name = args.triplet.get())
        {
            check_triplet(*triplet_name, database);
            return Triplet::from_canonical_name(*triplet_name);
        }

        return default_host_triplet(args, database);
    }

    Triplet default_host_triplet(const VcpkgCmdArguments& args, const TripletDatabase& database)
    {
        auto host_triplet_name = args.host_triplet.value_or(system_triplet_canonical_name());
        check_triplet(host_triplet_name, database);
        return Triplet::from_canonical_name(host_triplet_name);
    }

    void print_default_triplet_warning(const VcpkgCmdArguments& args, const TripletDatabase& database)
    {
        (void)args;
        (void)database;
#if defined(_WIN32)
        if (!args.triplet.has_value())
        {
            // Remove this warning in March 2024
            // The triplet is not set by --triplet or VCPKG_DEFAULT_TRIPLET
            msg::println_warning(msgDefaultTripletChanged, msg::triplet = default_host_triplet(args, database));
        }
#endif // ^^^ _WIN32
    }

    TripletFile::TripletFile(StringView name, StringView location) : name(name.data(), name.size()), location(location)
    {
    }

    Path TripletFile::get_full_path() const { return location / (name + ".cmake"); }

    Path TripletDatabase::get_triplet_file_path(Triplet triplet) const
    {
        auto it = Util::find_if(available_triplets,
                                [&](const TripletFile& tf) { return tf.name == triplet.canonical_name(); });

        if (it == available_triplets.end())
        {
            Checks::msg_exit_with_message(
                VCPKG_LINE_INFO, msgTripletFileNotFound, msg::triplet = triplet.canonical_name());
        }

        return it->get_full_path();
    }

    bool TripletDatabase::is_valid_triplet_name(StringView name) const
    {
        return is_valid_triplet_canonical_name(Strings::ascii_to_lowercase(name));
    }

    bool TripletDatabase::is_valid_triplet_canonical_name(StringView name) const
    {
        return Util::any_of(available_triplets, [=](const TripletFile& tf) { return tf.name == name; });
    }
}
