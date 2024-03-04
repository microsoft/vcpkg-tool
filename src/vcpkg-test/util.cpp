#include <vcpkg-test/util.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <vcpkg/statusparagraph.h>
#include <vcpkg/tools.h>

#include <stdlib.h>

#include <iostream>
#include <memory>
#include <numeric>
#include <set>
#include <vector>

namespace vcpkg::Test
{
    const Triplet ARM64_WINDOWS = Triplet::from_canonical_name("arm64-windows");
    const Triplet X86_WINDOWS = Triplet::from_canonical_name("x86-windows");
    const Triplet X64_WINDOWS = Triplet::from_canonical_name("x64-windows");
    const Triplet X64_WINDOWS_STATIC = Triplet::from_canonical_name("x64-windows-static");
    const Triplet X64_WINDOWS_STATIC_MD = Triplet::from_canonical_name("x64-windows-static-md");
    const Triplet X64_UWP = Triplet::from_canonical_name("x64-uwp");
    const Triplet X86_UWP = Triplet::from_canonical_name("x86-uwp");
    const Triplet ARM_UWP = Triplet::from_canonical_name("arm-uwp");
    const Triplet X64_ANDROID = Triplet::from_canonical_name("x64-android");
    const Triplet X64_OSX = Triplet::from_canonical_name("x64-osx");
    const Triplet X64_LINUX = Triplet::from_canonical_name("x64-linux");

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

    ExpectedL<std::unique_ptr<SourceControlFile>> test_parse_control_file(
        const std::vector<std::unordered_map<std::string, std::string>>& v)
    {
        std::vector<vcpkg::Paragraph> pghs;
        for (auto&& p : v)
        {
            pghs.emplace_back();
            for (auto&& kv : p)
            {
                pghs.back().emplace(kv.first, std::make_pair(kv.second, vcpkg::TextRowCol{}));
            }
        }
        return vcpkg::SourceControlFile::parse_control_file("", std::move(pghs));
    }

    std::unique_ptr<vcpkg::StatusParagraph> make_status_pgh(const char* name,
                                                            const char* depends,
                                                            const char* default_features,
                                                            const char* triplet)
    {
        return std::make_unique<StatusParagraph>(StringLiteral{"test"},
                                                 Paragraph{{"Package", {name, {}}},
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
        return std::make_unique<StatusParagraph>(StringLiteral{"test"},
                                                 Paragraph{{"Package", {name, {}}},
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
        // copy name before moving scfl
        auto name = scfl.to_name();
        REQUIRE(!Util::Maps::contains(map, name));
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

    std::vector<FullPackageSpec> parse_test_fspecs(StringView sv)
    {
        std::vector<FullPackageSpec> ret;
        ParserBase parser(sv, "test");
        while (!parser.at_eof())
        {
            auto opt = parse_qualified_specifier(parser);
            REQUIRE(opt.has_value());
            ret.push_back(opt.get()->to_full_spec(X86_WINDOWS, ImplicitDefault::YES).value_or_exit(VCPKG_LINE_INFO));
        }

        return ret;
    }

    const Path& base_temporary_directory() noexcept
    {
        const static Path BASE_TEMPORARY_DIRECTORY = internal_base_temporary_directory();
        return BASE_TEMPORARY_DIRECTORY;
    }

    static void check_json_eq(const Json::Value& l, const Json::Value& r, std::string& path, bool ordered);

    static void double_set_difference(const std::set<std::string>& a,
                                      const std::set<std::string>& b,
                                      std::vector<std::string>& a_extra,
                                      std::vector<std::string>& b_extra)
    {
        std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(a_extra));
        std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(b_extra));
    }

    static void check_json_eq(const Json::Object& l, const Json::Object& r, std::string& path, bool ordered)
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
            std::vector<std::string> l_extra, r_extra;
            double_set_difference(keys_l, keys_r, l_extra, r_extra);
            CHECK(l_extra == std::vector<std::string>{});
            CHECK(r_extra == std::vector<std::string>{});
            if (ordered && keys_l == keys_r)
            {
                size_t index = 0;
                for (auto i_l = l.begin(), i_r = r.begin(); i_l != l.end() && i_r != r.end(); ++i_l, ++i_r)
                {
                    if ((*i_l).first != (*i_r).first)
                    {
                        INFO("index = " << index);
                        CHECK((*i_l).first.to_string() == (*i_r).first.to_string());
                    }
                    ++index;
                }
            }
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
                check_json_eq(*vl, *vr, path, ordered);
                path.resize(orig_path_len);
            }
        }
    }
    static void check_json_eq(const Json::Array& l, const Json::Array& r, std::string& path, bool ordered)
    {
        {
            INFO(path)
            CHECK(l.size() == r.size());
        }
        const size_t orig_path_len = path.size();
        for (size_t i = 0; i < l.size() && i < r.size(); ++i)
        {
            fmt::format_to(std::back_inserter(path), "[{}]", i);
            check_json_eq(r[i], l[i], path, ordered);
            path.resize(orig_path_len);
        }
    }
    static void check_json_eq(const Json::Value& l, const Json::Value& r, std::string& path, bool ordered)
    {
        if (l.is_object() && r.is_object())
        {
            check_json_eq(l.object(VCPKG_LINE_INFO), r.object(VCPKG_LINE_INFO), path, ordered);
        }
        else if (l.is_array() && r.is_array())
        {
            check_json_eq(l.array(VCPKG_LINE_INFO), r.array(VCPKG_LINE_INFO), path, ordered);
        }
        else if (l != r)
        {
            INFO(path);
            INFO("l = " << Json::stringify(l));
            INFO("r = " << Json::stringify(r));
            CHECK(false);
        }
    }

    void check_json_eq(const Json::Value& l, const Json::Value& r)
    {
        std::string path = "$";
        check_json_eq(l, r, path, false);
    }
    void check_json_eq(const Json::Object& l, const Json::Object& r)
    {
        std::string path = "$";
        check_json_eq(l, r, path, false);
    }
    void check_json_eq(const Json::Array& l, const Json::Array& r)
    {
        std::string path = "$";
        check_json_eq(l, r, path, false);
    }

    void check_json_eq_ordered(const Json::Value& l, const Json::Value& r)
    {
        std::string path = "$";
        check_json_eq(l, r, path, true);
    }
    void check_json_eq_ordered(const Json::Object& l, const Json::Object& r)
    {
        std::string path = "$";
        check_json_eq(l, r, path, true);
    }
    void check_json_eq_ordered(const Json::Array& l, const Json::Array& r)
    {
        std::string path = "$";
        check_json_eq(l, r, path, true);
    }

    Optional<std::string> diff_lines(StringView a, StringView b)
    {
        auto lines_a = Strings::split_keep_empty(a, '\n');
        auto lines_b = Strings::split_keep_empty(b, '\n');

        std::vector<std::vector<size_t>> edits;
        auto& first_row = edits.emplace_back();
        first_row.resize(lines_b.size() + 1);
        std::iota(first_row.begin(), first_row.end(), 0);
        for (size_t i = 0; i < lines_a.size(); ++i)
        {
            edits.emplace_back().resize(lines_b.size() + 1);
            edits[i + 1][0] = edits[i][0] + 1;
            for (size_t j = 0; j < lines_b.size(); ++j)
            {
                size_t p = edits[i + 1][j] + 1;
                size_t m = edits[i][j + 1] + 1;
                if (m < p) p = m;
                if (lines_a[i] == lines_b[j] && edits[i][j] < p) p = edits[i][j];
                edits[i + 1][j + 1] = p;
            }
        }

        size_t i = lines_a.size();
        size_t j = lines_b.size();
        if (edits[i][j] == 0) return nullopt;

        std::vector<std::string> lines;

        while (i > 0 && j > 0)
        {
            if (edits[i][j] == edits[i - 1][j - 1] && lines_a[i - 1] == lines_b[j - 1])
            {
                --j;
                --i;
                lines.emplace_back(" " + lines_a[i]);
            }
            else if (edits[i][j] == edits[i - 1][j] + 1)
            {
                --i;
                lines.emplace_back("-" + lines_a[i]);
            }
            else
            {
                --j;
                lines.emplace_back("+" + lines_b[j]);
            }
        }
        for (; i > 0; --i)
        {
            lines.emplace_back("-" + lines_a[i - 1]);
        }
        for (; j > 0; --j)
        {
            lines.emplace_back("+" + lines_b[j - 1]);
        }
        std::string ret;
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
        {
            ret.append(*it);
            ret.push_back('\n');
        }
        return ret;
    }
}

TEST_CASE ("diff algorithm", "[diff]")
{
    using namespace vcpkg::Test;
    CHECK(!diff_lines("hello", "hello"));
    CHECK(!diff_lines("hello\n", "hello\n"));
    CHECK(!diff_lines("hello\n\nworld", "hello\n\nworld"));
    {
        auto a = diff_lines("hello\na\nworld", "hello\nworld");
        REQUIRE(a);
        CHECK(*a.get() == " hello\n-a\n world\n");
    }
    {
        auto a = diff_lines("hello\nworld", "hello\na\nworld");
        REQUIRE(a);
        CHECK(*a.get() == " hello\n+a\n world\n");
    }
}
