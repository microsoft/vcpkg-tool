#include <catch2/catch.hpp>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <vcpkg/statusparagraph.h>

#include <stdlib.h>

#include <iostream>
#include <memory>
#include <set>
#include <vector>

#include <vcpkg-test/util.h>

namespace vcpkg::Test
{
    const Triplet X86_WINDOWS = Triplet::from_canonical_name("x86-windows");
    const Triplet X64_WINDOWS = Triplet::from_canonical_name("x64-windows");
    const Triplet X86_UWP = Triplet::from_canonical_name("x86-uwp");
    const Triplet ARM_UWP = Triplet::from_canonical_name("arm-uwp");
    const Triplet X64_ANDROID = Triplet::from_canonical_name("x64-android");

    std::unique_ptr<SourceControlFile> make_control_file(
        const char* name,
        const char* depends,
        const std::vector<std::pair<const char*, const char*>>& features,
        const std::vector<const char*>& default_features)
    {
        using Pgh = std::unordered_map<std::string, std::string>;
        std::vector<Pgh> scf_pghs;
        scf_pghs.push_back(Pgh{{"Source", name},
                               {"Version", "0"},
                               {"Build-Depends", depends},
                               {"Default-Features", Strings::join(", ", default_features)}});
        for (auto&& feature : features)
        {
            scf_pghs.push_back(Pgh{
                {"Feature", feature.first},
                {"Description", "feature"},
                {"Build-Depends", feature.second},
            });
        }
        auto m_pgh = test_parse_control_file(std::move(scf_pghs));
        REQUIRE(m_pgh.has_value());
        return std::move(*m_pgh.get());
    }

    std::unique_ptr<vcpkg::StatusParagraph> make_status_pgh(const char* name,
                                                            const char* depends,
                                                            const char* default_features,
                                                            const char* triplet)
    {
        return std::make_unique<StatusParagraph>(Parse::Paragraph{{"Package", {name, {}}},
                                                                  {"Version", {"1", {}}},
                                                                  {"Architecture", {triplet, {}}},
                                                                  {"Multi-Arch", {"same", {}}},
                                                                  {"Depends", {depends, {}}},
                                                                  {"Default-Features", {default_features, {}}},
                                                                  {"Status", {"install ok installed", {}}}});
    }

    std::unique_ptr<StatusParagraph> make_status_feature_pgh(const char* name,
                                                             const char* feature,
                                                             const char* depends,
                                                             const char* triplet)
    {
        return std::make_unique<StatusParagraph>(Parse::Paragraph{{"Package", {name, {}}},
                                                                  {"Feature", {feature, {}}},
                                                                  {"Architecture", {triplet, {}}},
                                                                  {"Multi-Arch", {"same", {}}},
                                                                  {"Depends", {depends, {}}},
                                                                  {"Status", {"install ok installed", {}}}});
    }

    PackageSpec PackageSpecMap::emplace(const char* name,
                                        const char* depends,
                                        const std::vector<std::pair<const char*, const char*>>& features,
                                        const std::vector<const char*>& default_features)
    {
        auto scfl = SourceControlFileAndLocation{make_control_file(name, depends, features, default_features), ""};
        return emplace(std::move(scfl));
    }

    PackageSpec PackageSpecMap::emplace(vcpkg::SourceControlFileAndLocation&& scfl)
    {
        const auto& name = scfl.source_control_file->core_paragraph->name;
        REQUIRE(map.find(name) == map.end());
        map.emplace(name, std::move(scfl));
        return {name, triplet};
    }

    static Path internal_base_temporary_directory()
    {
#if defined(_WIN32)
        return Path(vcpkg::get_environment_variable("TEMP").value_or_exit(VCPKG_LINE_INFO)) / "vcpkg-test";
#else
        return "/tmp/vcpkg-test";
#endif
    }

    const Path& base_temporary_directory() noexcept
    {
        const static Path BASE_TEMPORARY_DIRECTORY = internal_base_temporary_directory();
        return BASE_TEMPORARY_DIRECTORY;
    }
    static void check_json_eq(const Json::Value& l, const Json::Value& r, std::string& path);
    static void check_json_eq(const Json::Value& l, const Json::Value& r, std::string& path);
    static void check_json_eq(const Json::Value& l, const Json::Value& r, std::string& path);

    static void check_json_eq(const Json::Object& l, const Json::Object& r, std::string& path)
    {
        std::set<std::string> keys_l;
        for (auto&& kv : l)
        {
            keys_l.insert(kv.first.to_string());
        }
        std::set<std::string> keys_r;
        for (auto&& kv : r)
        {
            keys_r.insert(kv.first.to_string());
        }
        {
            INFO(path)
            CHECK(keys_l == keys_r);
        }
        const size_t orig_path_len = path.size();
        for (auto&& key : keys_l)
        {
            auto vl = l.get(key);
            auto vr = r.get(key);
            if (vl && vr)
            {
                path.push_back('.');
                path.append(key);
                check_json_eq(*vl, *vr, path);
                path.resize(orig_path_len);
            }
        }
    }
    static void check_json_eq(const Json::Array& l, const Json::Array& r, std::string& path)
    {
        {
            INFO(path)
            CHECK(l.size() == r.size());
        }
        const size_t orig_path_len = path.size();
        for (size_t i = 0; i < l.size() && i < r.size(); ++i)
        {
            Strings::append(path, '[', i, ']');
            check_json_eq(r[i], l[i], path);
            path.resize(orig_path_len);
        }
    }
    static void check_json_eq(const Json::Value& l, const Json::Value& r, std::string& path)
    {
        if (l.is_object() && r.is_object())
        {
            check_json_eq(l.object(), r.object(), path);
        }
        else if (l.is_array() && r.is_array())
        {
            check_json_eq(l.array(), r.array(), path);
        }
        else
        {
            INFO(path);
            REQUIRE(l == r);
        }
    }

    void check_json_eq(const Json::Value& l, const Json::Value& r)
    {
        std::string path = "$";
        check_json_eq(l, r, path);
    }
    void check_json_eq(const Json::Object& l, const Json::Object& r)
    {
        std::string path = "$";
        check_json_eq(l, r, path);
    }
    void check_json_eq(const Json::Array& l, const Json::Array& r)
    {
        std::string path = "$";
        check_json_eq(l, r, path);
    }

}
