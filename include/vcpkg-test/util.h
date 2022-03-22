#include <vcpkg/base/system_headers.h>

#include <catch2/catch.hpp>

#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/pragmas.h>

#include <vcpkg/statusparagraph.h>

#include <iomanip>
#include <memory>

#define CHECK_EC(ec)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (ec)                                                                                                        \
        {                                                                                                              \
            FAIL(ec.message());                                                                                        \
        }                                                                                                              \
    } while (0)

namespace Catch
{
    template<>
    struct StringMaker<vcpkg::FullPackageSpec>
    {
        static std::string convert(vcpkg::FullPackageSpec const& value)
        {
            return vcpkg::Strings::concat(value.package_spec.name(),
                                          '[',
                                          vcpkg::Strings::join(",", value.features),
                                          "]:",
                                          value.package_spec.triplet());
        }
    };

    template<>
    struct StringMaker<vcpkg::FeatureSpec>
    {
        static std::string convert(vcpkg::FeatureSpec const& value)
        {
            return vcpkg::Strings::concat(value.spec().name(), '[', value.feature(), "]:", value.spec().triplet());
        }
    };

    template<>
    struct StringMaker<vcpkg::Triplet>
    {
        static const std::string& convert(const vcpkg::Triplet& triplet) { return triplet.canonical_name(); }
    };

    template<>
    struct StringMaker<vcpkg::LocalizedString>
    {
        static const std::string convert(const vcpkg::LocalizedString& value) { return "LL\"" + value.data() + "\""; }
    };
}

namespace vcpkg
{
    inline std::ostream& operator<<(std::ostream& os, const LocalizedString& value)
    {
        return os << "LL" << std::quoted(value.data());
    }
}

namespace vcpkg::Test
{
    std::unique_ptr<SourceControlFile> make_control_file(
        const char* name,
        const char* depends,
        const std::vector<std::pair<const char*, const char*>>& features = {},
        const std::vector<const char*>& default_features = {});

    inline auto test_parse_control_file(const std::vector<std::unordered_map<std::string, std::string>>& v)
    {
        std::vector<vcpkg::Paragraph> pghs;
        for (auto&& p : v)
        {
            pghs.emplace_back();
            for (auto&& kv : p)
                pghs.back().emplace(kv.first, std::make_pair(kv.second, vcpkg::TextRowCol{}));
        }
        return vcpkg::SourceControlFile::parse_control_file("", std::move(pghs));
    }

    std::unique_ptr<vcpkg::StatusParagraph> make_status_pgh(const char* name,
                                                            const char* depends = "",
                                                            const char* default_features = "",
                                                            const char* triplet = "x86-windows");

    std::unique_ptr<vcpkg::StatusParagraph> make_status_feature_pgh(const char* name,
                                                                    const char* feature,
                                                                    const char* depends = "",
                                                                    const char* triplet = "x86-windows");

    extern const Triplet X86_WINDOWS;
    extern const Triplet X64_WINDOWS;
    extern const Triplet X86_UWP;
    extern const Triplet ARM_UWP;
    extern const Triplet X64_ANDROID;

    /// <summary>
    /// Map of source control files by their package name.
    /// </summary>
    struct PackageSpecMap
    {
        std::unordered_map<std::string, SourceControlFileAndLocation> map;
        Triplet triplet;
        PackageSpecMap(Triplet t = X86_WINDOWS) noexcept : triplet(t) { }

        PackageSpec emplace(const char* name,
                            const char* depends = "",
                            const std::vector<std::pair<const char*, const char*>>& features = {},
                            const std::vector<const char*>& default_features = {});

        PackageSpec emplace(vcpkg::SourceControlFileAndLocation&& scfl);
    };

    template<class T, class S>
    T&& unwrap(vcpkg::ExpectedT<T, S>&& p)
    {
        REQUIRE(p.has_value());
        return std::move(*p.get());
    }

    template<class T>
    T&& unwrap(vcpkg::Optional<T>&& opt)
    {
        REQUIRE(opt.has_value());
        return std::move(*opt.get());
    }

    inline std::vector<FullPackageSpec> parse_test_fspecs(StringView sv, Triplet t = X86_WINDOWS)
    {
        std::vector<FullPackageSpec> ret;
        ParserBase parser(sv, "test");
        while (!parser.at_eof())
        {
            auto opt = parse_qualified_specifier(parser);
            REQUIRE(opt.has_value());
            ret.push_back(unwrap(opt.get()->to_full_spec(t, ImplicitDefault::YES)));
        }
        return ret;
    }

    template<class R1, class R2>
    void check_ranges(const R1& r1, const R2& r2)
    {
        CHECK(r1.size() == r2.size());
        auto it1 = r1.begin();
        auto e1 = r1.end();
        auto it2 = r2.begin();
        auto e2 = r2.end();
        for (; it1 != e1 && it2 != e2; ++it1, ++it2)
        {
            CHECK(*it1 == *it2);
        }
    }

    void check_json_eq(const Json::Value& l, const Json::Value& r);
    void check_json_eq(const Json::Object& l, const Json::Object& r);
    void check_json_eq(const Json::Array& l, const Json::Array& r);

    void check_json_eq_ordered(const Json::Value& l, const Json::Value& r);
    void check_json_eq_ordered(const Json::Object& l, const Json::Object& r);
    void check_json_eq_ordered(const Json::Array& l, const Json::Array& r);

    const Path& base_temporary_directory() noexcept;
}
